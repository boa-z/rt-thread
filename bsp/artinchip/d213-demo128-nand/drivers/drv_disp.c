/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D213 LVDS display, DE timing and framebuffer driver.
 *
 * The DE register programming is done by the unmodified vendor HAL
 * (vendor/display/aic_hal_de.c).  LVDS is a small register sequence
 * derived from the vendor drv_lvds.c.  The RT-Thread graphic device,
 * framebuffer reservation, VSync completion and cache maintenance are
 * BSP code.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <stdint.h>
#include <string.h>

#include "board.h"
#include "aic_cache.h"
#include "drv_disp.h"
#include "aic_hal.h"
#include "aic_hal_de.h"
#include "aic_hal_gpio.h"
#include "aic_hal_lvds.h"
#include "aic_disp_clk.h"
#include "artinchip_fb.h"

#ifndef BSP_DISPLAY_WIDTH
#define BSP_DISPLAY_WIDTH        1024
#endif
#ifndef BSP_DISPLAY_HEIGHT
#define BSP_DISPLAY_HEIGHT       600
#endif
#ifndef BSP_DISPLAY_PIXCLOCK
#define BSP_DISPLAY_PIXCLOCK     52000000
#endif
#ifndef BSP_DISPLAY_HFP
#define BSP_DISPLAY_HFP          160
#endif
#ifndef BSP_DISPLAY_HBP
#define BSP_DISPLAY_HBP          160
#endif
#ifndef BSP_DISPLAY_HSW
#define BSP_DISPLAY_HSW          20
#endif
#ifndef BSP_DISPLAY_VFP
#define BSP_DISPLAY_VFP          20
#endif
#ifndef BSP_DISPLAY_VBP
#define BSP_DISPLAY_VBP          12
#endif
#ifndef BSP_DISPLAY_VSW
#define BSP_DISPLAY_VSW          2
#endif
#ifndef BSP_DISPLAY_LVDS_LINK
#define BSP_DISPLAY_LVDS_LINK    1
#endif
#ifndef BSP_DISPLAY_LVDS_MODE
#define BSP_DISPLAY_LVDS_MODE    0
#endif
#ifndef BSP_DISPLAY_FB_COUNT
#define BSP_DISPLAY_FB_COUNT     2
#endif
#ifndef AIC_FB_RESERVED_SIZE
#define AIC_FB_RESERVED_SIZE     0
#endif
#ifndef BSP_DISPLAY_PANEL_ENABLE_GPIO
#define BSP_DISPLAY_PANEL_ENABLE_GPIO "PC.7"
#endif

#define DISP_FB_BPP              32
#define DISP_ALIGN_64(x)         (((x) + 63UL) & ~63UL)
#define DISP_VSYNC_TIMEOUT_MS    100

extern char __fb_start[];
extern char __fb_end[];

struct aic_disp
{
    struct rt_device dev;
    struct rt_device_graphic_info info;
    struct rt_completion vsync;
    rt_uint32_t width;
    rt_uint32_t height;
    rt_uint32_t bpp;
    rt_uint32_t stride;
    rt_uint32_t fb_count;
    rt_uint32_t fb_size;
    void *fb[2];
    volatile rt_uint32_t vsync_count;
    volatile rt_uint32_t underflow_count;
    int active;
    rt_bool_t power_on;
    rt_bool_t inited;
};

static struct aic_disp g_disp;

static void disp_panel_gpio(int on)
{
    long pin;
    unsigned int group;
    unsigned int offset;

    pin = hal_gpio_name2pin(BSP_DISPLAY_PANEL_ENABLE_GPIO);
    if (pin < 0)
        return;

    group = GPIO_GROUP(pin);
    offset = GPIO_GROUP_PIN(pin);

    hal_gpio_set_func(group, offset, 1);
    hal_gpio_set_bias_pull(group, offset, PIN_PULL_DIS);
    hal_gpio_set_drive_strength(group, offset, 3);
    hal_gpio_direction_output(group, offset);

#ifdef BSP_DISPLAY_PANEL_ENABLE_LOW
    if (on)
        hal_gpio_clr_output(group, offset);
    else
        hal_gpio_set_output(group, offset);
#else
    if (on)
        hal_gpio_set_output(group, offset);
    else
        hal_gpio_clr_output(group, offset);
#endif
}

static void disp_lvds_configure(void)
{
    u32 reg;
    const u32 lanes = LVDS_LANES(LVDS_D3, LVDS_CK, LVDS_D2, LVDS_D1, LVDS_D0);

    writel(lanes, LVDS_BASE + LVDS_0_SWAP);
    writel(lanes, LVDS_BASE + LVDS_1_SWAP);
    writel(0, LVDS_BASE + LVDS_0_POL_CTL);
    writel(0, LVDS_BASE + LVDS_1_POL_CTL);
    writel(0xFA, LVDS_BASE + LVDS_0_PHY_CTL);
    writel(0xFA, LVDS_BASE + LVDS_1_PHY_CTL);

    reg = readl(LVDS_BASE + LVDS_CTL);
    reg &= ~(LVDS_CTL_MODE_MASK | LVDS_CTL_LINK_MASK |
             LVDS_CTL_SWAP_MASK | LVDS_CTL_SYNC_MODE_MASK);
    reg |= LVDS_CTL_MODE(BSP_DISPLAY_LVDS_MODE);
    reg |= LVDS_CTL_LINK(BSP_DISPLAY_LVDS_LINK);
    reg |= LVDS_CTL_SYNC_MODE_EN(1);
    writel(reg, LVDS_BASE + LVDS_CTL);

    reg |= LVDS_CTL_EN;
    writel(reg, LVDS_BASE + LVDS_CTL);
}

static void disp_lvds_disable(void)
{
    u32 reg = readl(LVDS_BASE + LVDS_CTL);

    reg &= ~LVDS_CTL_EN;
    writel(reg, LVDS_BASE + LVDS_CTL);
}

static void disp_de_isr(int irq, void *param)
{
    struct aic_disp *disp = (struct aic_disp *)param;
    u32 status;

    (void)irq;
    status = de_timing_interrupt_status((void *)DE_BASE);
    de_timing_interrupt_clean_status((void *)DE_BASE, status);

    if (status & TIMING_INIT_UNDERFLOW_FLAG)
        disp->underflow_count++;

    if (status & TIMING_INIT_V_BLANK_FLAG)
    {
        disp->vsync_count++;
        rt_completion_done(&disp->vsync);
    }
}

static int disp_de_configure(struct aic_disp *disp)
{
    void *de = (void *)DE_BASE;

    de_config_update_enable(de, 0);

    de_config_timing(de, disp->width, disp->height,
                     BSP_DISPLAY_HFP, BSP_DISPLAY_HBP,
                     BSP_DISPLAY_VFP, BSP_DISPLAY_VBP,
                     BSP_DISPLAY_HSW, BSP_DISPLAY_VSW,
                     0, 0); /* hsync/vsync low, matching the panel */

    de_set_blending_size(de, disp->width, disp->height);
    de_set_ui_layer_size(de, disp->width, disp->height, 0, 0);
    de_set_ui_layer_format(de, MPP_FMT_XRGB_8888);
    de_ui_layer_set_rect(de, disp->width, disp->height, 0, 0,
                         disp->stride,
                         (u32)(uintptr_t)disp->fb[disp->active], 0);
    de_ui_layer_rect_enable(de, 0, 1);
    de_ui_bg_blending_enable(de, 0);
    /* XRGB: the upper byte is don't-care, so do not enable alpha. */
    de_ui_alpha_blending_enable(de, 255, AICFB_PIXEL_ALPHA_MODE, 0);
    de_ui_layer_enable(de, 1);
    de_colorbar_ctrl(de, 0);

    /* Match the vendor DE bring-up order. */
    de_config_prefetch_line_set(de, 2);
    de_soft_reset_ctrl(de, 1);
    de_set_qos(de);
    de_set_mode(de, 0); /* continuous mode */
    de_config_update_enable(de, 1);

    de_timing_enable_interrupt(de, 1, TIMING_INIT_V_BLANK_FLAG);
    de_timing_enable_interrupt(de, 1, TIMING_INIT_UNDERFLOW_FLAG);

    rt_hw_interrupt_install(IRQ_PLIC(DE_IRQn), disp_de_isr, disp, "de");
    rt_hw_interrupt_umask(IRQ_PLIC(DE_IRQn));

    disp->info.framebuffer = disp->fb[disp->active];

    return 0;
}

static int disp_apply_buffer(struct aic_disp *disp, int index)
{
    void *de = (void *)DE_BASE;

    if (index < 0 || index >= (int)disp->fb_count)
        return -RT_EINVAL;

    de_config_update_enable(de, 0);
    de_ui_layer_set_rect(de, disp->width, disp->height, 0, 0,
                         disp->stride,
                         (u32)(uintptr_t)disp->fb[index], 0);
    de_config_update_enable(de, 1);

    disp->active = index;
    disp->info.framebuffer = disp->fb[index];

    return RT_EOK;
}

void aic_disp_cache_clean(int index)
{
    struct aic_disp *disp = &g_disp;

    if (!disp->inited || index < 0 || index >= (int)disp->fb_count)
        return;

    aic_dcache_clean_range((uintptr_t)disp->fb[index], disp->fb_size);
}

void aic_disp_get_stats(rt_uint32_t *vsync_count,
                        rt_uint32_t *underflow_count, int *active)
{
    struct aic_disp *disp = &g_disp;

    if (vsync_count)
        *vsync_count = disp->vsync_count;
    if (underflow_count)
        *underflow_count = disp->underflow_count;
    if (active)
        *active = disp->active;
}

int aic_disp_wait_vsync(int timeout_ms)
{
    struct aic_disp *disp = &g_disp;
    rt_uint32_t seq;
    rt_tick_t deadline = 0;
    rt_bool_t forever;

    if (!disp->inited || !disp->power_on)
        return -RT_ERROR;

    /*
     * A completion records that at least one VSync happened, but it may
     * already be set by a VSync that occurred before this call.  Compare
     * the ISR sequence counter so the caller waits for a new frame.
     */
    seq = disp->vsync_count;
    forever = (timeout_ms < 0);
    if (!forever)
    {
        rt_int32_t ticks = rt_tick_from_millisecond(timeout_ms);

        if (ticks <= 0)
            ticks = 1;
        deadline = rt_tick_get() + (rt_tick_t)ticks;
    }

    for (;;)
    {
        rt_int32_t wait_ticks;

        if (disp->vsync_count != seq)
            return RT_EOK;

        if (forever)
        {
            wait_ticks = RT_WAITING_FOREVER;
        }
        else
        {
            rt_tick_t now = rt_tick_get();

            if ((rt_int32_t)(deadline - now) <= 0)
                return -RT_ETIMEOUT;
            wait_ticks = (rt_int32_t)(deadline - now);
        }

        if (rt_completion_wait(&disp->vsync, wait_ticks) != RT_EOK)
            return -RT_ETIMEOUT;
    }
}

int aic_disp_pan(int index, int wait_vsync)
{
    struct aic_disp *disp = &g_disp;
    int ret = RT_EOK;

    if (!disp->inited || !disp->power_on)
        return -RT_ERROR;

    aic_disp_cache_clean(index);

    if (wait_vsync)
        ret = aic_disp_wait_vsync(DISP_VSYNC_TIMEOUT_MS);

    /*
     * Even if VSync did not arrive (for example during early bring-up),
     * apply the buffer and report the timeout to the caller.  This makes
     * the failure observable without silently leaving the old frame.
     */
    {
        int apply_ret = disp_apply_buffer(disp, index);
        return (ret == RT_EOK) ? apply_ret : ret;
    }
}

int aic_disp_colorbar(int enable)
{
    struct aic_disp *disp = &g_disp;
    void *de = (void *)DE_BASE;

    if (!disp->inited || !disp->power_on)
        return -RT_ERROR;

    de_config_update_enable(de, 0);
    if (enable)
    {
        de_ui_layer_enable(de, 0);
        de_colorbar_ctrl(de, 1);
    }
    else
    {
        de_colorbar_ctrl(de, 0);
        de_ui_layer_enable(de, 1);
    }
    de_config_update_enable(de, 1);

    return RT_EOK;
}

static int disp_power(struct aic_disp *disp, int on)
{
    void *de = (void *)DE_BASE;

    if (on == disp->power_on)
        return RT_EOK;

    if (on)
    {
        int ret;

        disp_panel_gpio(0);
        ret = aic_disp_clk_set(BSP_DISPLAY_PIXCLOCK, BSP_DISPLAY_LVDS_LINK,
                               RT_NULL);
        if (ret != 0)
        {
            rt_kprintf("lcd: display clock setup failed (%d)\n", ret);
            return -RT_ERROR;
        }

        ret = hal_clk_set_freq(CLK_DE, 200000000);
        if (ret != 0)
        {
            rt_kprintf("lcd: DE clock rate setup failed (%d)\n", ret);
            return -RT_ERROR;
        }

        ret = hal_clk_enable_deassertrst(CLK_DE);
        if (ret != 0)
        {
            rt_kprintf("lcd: DE clock enable failed (%d)\n", ret);
            return -RT_ERROR;
        }

        ret = hal_clk_enable_deassertrst(CLK_LVDS);
        if (ret != 0)
        {
            rt_kprintf("lcd: LVDS clock enable failed (%d)\n", ret);
            return -RT_ERROR;
        }

        disp_lvds_configure();
        disp_de_configure(disp);
        aic_dcache_clean_range((uintptr_t)disp->fb[disp->active],
                               disp->fb_size);
        de_timing_enable(de, 1);

        rt_thread_mdelay(60);
        disp_panel_gpio(1);
        disp->power_on = RT_TRUE;
    }
    else
    {
        disp_panel_gpio(0);
        de_timing_enable(de, 0);
        rt_hw_interrupt_mask(IRQ_PLIC(DE_IRQn));
        disp_lvds_disable();

        /*
         * Assert the resets and gate the clocks.  hal_clk_disable() alone
         * only clears the gate; the vendor LVDS/DE paths assert reset too.
         */
        hal_clk_disable_assertrst(CLK_LVDS);
        hal_clk_disable(CLK_LVDS);
        hal_clk_disable_assertrst(CLK_DE);
        hal_clk_disable(CLK_DE);
        disp->power_on = RT_FALSE;
    }

    return RT_EOK;
}

static rt_err_t disp_control(rt_device_t dev, int cmd, void *args)
{
    struct aic_disp *disp = rt_container_of(dev, struct aic_disp, dev);

    switch (cmd)
    {
    case RTGRAPHIC_CTRL_GET_INFO:
        if (args == RT_NULL)
            return -RT_EINVAL;
        *(struct rt_device_graphic_info *)args = disp->info;
        return RT_EOK;

    case RTGRAPHIC_CTRL_GET_MODE:
        if (args == RT_NULL)
            return -RT_EINVAL;
        *(rt_uint32_t *)args = RTGRAPHIC_PIXEL_FORMAT_ARGB888;
        return RT_EOK;

    case RTGRAPHIC_CTRL_PAN_DISPLAY:
    {
        rt_uint32_t index;

        if (args != RT_NULL)
            index = *(rt_uint32_t *)args;
        else
            index = (disp->active + 1) % disp->fb_count;

        return aic_disp_pan((int)index, 1);
    }

    case RTGRAPHIC_CTRL_WAIT_VSYNC:
        return aic_disp_wait_vsync(DISP_VSYNC_TIMEOUT_MS);

    case RTGRAPHIC_CTRL_RECT_UPDATE:
        aic_disp_cache_clean(disp->active);
        return RT_EOK;

    case RTGRAPHIC_CTRL_POWERON:
        return disp_power(disp, 1);

    case RTGRAPHIC_CTRL_POWEROFF:
        return disp_power(disp, 0);

    case RTGRAPHIC_CTRL_GET_STATUS:
        if (args != RT_NULL)
            *(rt_uint32_t *)args = disp->power_on ? 1 : 0;
        return RT_EOK;

    case RTGRAPHIC_CTRL_SET_BRIGHTNESS:
        /*
         * The demo128 panel enable pin is a GPIO, not a PWM backlight.
         * Do not report a brightness level that the hardware cannot set.
         */
        return -RT_ENOSYS;

    default:
        return -RT_EINVAL;
    }
}

static rt_err_t disp_init(rt_device_t dev)
{
    (void)dev;
    return RT_EOK;
}

static rt_err_t disp_open(rt_device_t dev, rt_uint16_t oflag)
{
    (void)dev;
    (void)oflag;
    return RT_EOK;
}

static rt_err_t disp_close(rt_device_t dev)
{
    (void)dev;
    return RT_EOK;
}

static const struct rt_device_ops disp_dev_ops =
{
    .init = disp_init,
    .open = disp_open,
    .close = disp_close,
    .read = RT_NULL,
    .write = RT_NULL,
    .control = disp_control,
};

int aic_disp_init(void)
{
    struct aic_disp *disp = &g_disp;
    size_t reserved;

    if (disp->inited)
        return RT_EOK;

    if (AIC_FB_RESERVED_SIZE < BSP_DISPLAY_WIDTH * BSP_DISPLAY_HEIGHT * 4)
    {
        rt_kprintf("lcd: framebuffer reserve too small\n");
        return -RT_ENOMEM;
    }

    reserved = (size_t)(__fb_end - __fb_start);
    disp->width = BSP_DISPLAY_WIDTH;
    disp->height = BSP_DISPLAY_HEIGHT;
    disp->bpp = DISP_FB_BPP;
    disp->stride = DISP_ALIGN_64(disp->width * (disp->bpp / 8));
    disp->fb_size = DISP_ALIGN_64(disp->stride * disp->height);
    disp->fb_count = BSP_DISPLAY_FB_COUNT;
    if (disp->fb_count > 2)
        disp->fb_count = 2;

    if (disp->fb_size * disp->fb_count > reserved)
    {
        rt_kprintf("lcd: need %u bytes, reserved %u bytes\n",
                   (unsigned int)(disp->fb_size * disp->fb_count),
                   (unsigned int)reserved);
        return -RT_ENOMEM;
    }

    disp->fb[0] = (void *)__fb_start;
    disp->fb[1] = (void *)((char *)__fb_start + disp->fb_size);

    memset(disp->fb[0], 0, disp->fb_size * disp->fb_count);
    aic_dcache_clean_invalidate_range((uintptr_t)disp->fb[0],
                                      disp->fb_size * disp->fb_count);

    rt_completion_init(&disp->vsync);

    if (disp_power(disp, 1) != RT_EOK)
    {
        rt_kprintf("lcd: display power-up failed\n");
        return -RT_ERROR;
    }

    disp->info.pixel_format = RTGRAPHIC_PIXEL_FORMAT_ARGB888;
    disp->info.bits_per_pixel = DISP_FB_BPP;
    disp->info.pitch = disp->stride;
    disp->info.width = disp->width;
    disp->info.height = disp->height;
    disp->info.framebuffer = disp->fb[0];
    disp->info.smem_len = disp->fb_size * disp->fb_count;

    disp->dev.type = RT_Device_Class_Graphic;
    disp->dev.ops = &disp_dev_ops;
    disp->dev.user_data = disp;
    disp->info.pixel_format = RTGRAPHIC_PIXEL_FORMAT_ARGB888;
    disp->info.bits_per_pixel = DISP_FB_BPP;
    disp->info.pitch = disp->stride;
    disp->info.width = disp->width;
    disp->info.height = disp->height;
    disp->info.framebuffer = disp->fb[0];
    disp->info.smem_len = disp->fb_size * disp->fb_count;

    if (rt_device_register(&disp->dev, "lcd", RT_DEVICE_FLAG_RDWR) != RT_EOK)
    {
        rt_kprintf("lcd: device register failed\n");
        return -RT_ERROR;
    }

    disp->inited = RT_TRUE;

    rt_kprintf("lcd: %ux%u ARGB8888, %u buffer(s), stride %u\n",
               disp->width, disp->height, disp->fb_count, disp->stride);
    rt_kprintf("lcd: fb 0x%08x/0x%08x, reserved %u bytes\n",
               (unsigned int)(uintptr_t)disp->fb[0],
               (unsigned int)(uintptr_t)disp->fb[1],
               (unsigned int)reserved);

    return RT_EOK;
}

struct rt_device *aic_disp_device(void)
{
    return g_disp.inited ? &g_disp.dev : RT_NULL;
}

int aic_disp_get_info(struct aic_disp_info *info)
{
    struct aic_disp *disp = &g_disp;

    if (!disp->inited || info == RT_NULL)
        return -RT_ERROR;

    info->width = disp->width;
    info->height = disp->height;
    info->bpp = disp->bpp;
    info->stride = disp->stride;
    info->fb_count = disp->fb_count;
    info->fb_size = disp->fb_size;
    info->fb[0] = disp->fb[0];
    info->fb[1] = disp->fb[1];

    return RT_EOK;
}

void *aic_disp_get_framebuffer(int index)
{
    struct aic_disp *disp = &g_disp;

    if (!disp->inited || index < 0 || index >= (int)disp->fb_count)
        return RT_NULL;

    return disp->fb[index];
}

int aic_disp_get_fb_count(void)
{
    return g_disp.inited ? (int)g_disp.fb_count : 0;
}

int rt_hw_disp_init(void)
{
    return aic_disp_init();
}
INIT_DEVICE_EXPORT(rt_hw_disp_init);
