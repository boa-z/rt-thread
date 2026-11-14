/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D213 LVDS display, DE timing and framebuffer driver.
 *
 * The DE register programming is done by the unmodified vendor HAL
 * (libraries/soc/d21x/display/aic_hal_de.c).  LVDS is a small register sequence
 * derived from the vendor drv_lvds.c.  The RT-Thread graphic device,
 * framebuffer reservation, VSync completion and cache maintenance are
 * BSP code.
 */

#include <rtthread.h>
#include <interrupt.h>
#include <rtdevice.h>
#include <stdint.h>
#include <string.h>

#include "aic_soc.h"
#include "aic_board.h"
#include "aic_cache.h"
#include "drv_disp.h"
#include "aic_hal.h"
#include "aic_hal_de.h"
#include "aic_hal_lvds.h"
#include "aic_disp_clk.h"
#include "artinchip_fb.h"
#include "aic_buffer.h"

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
static struct rt_mutex disp_control_mutex;
static struct aicfb_layer_data fb_layers[2];
static struct aicfb_alpha_config fb_alpha = { AICFB_LAYER_TYPE_UI, 0, 0, 255 };
static rt_uint32_t fb_bg_blend;
static const void *layer_owner[2];
static rt_bool_t layer_uncertain[2];
static int disp_layer_commit(struct aic_disp *d, struct aicfb_layer_data *layer);


static int disp_panel_gpio(int on)
{
    return aic_board_panel.set_power ? aic_board_panel.set_power(on) : RT_EOK;
}

static void disp_lvds_configure(void)
{
    u32 reg;
    const u32 lanes = aic_board_panel.lanes;

    writel(lanes, LVDS_BASE + LVDS_0_SWAP);
    writel(lanes, LVDS_BASE + LVDS_1_SWAP);
    writel(aic_board_panel.polarity, LVDS_BASE + LVDS_0_POL_CTL);
    writel(aic_board_panel.polarity, LVDS_BASE + LVDS_1_POL_CTL);
    writel(aic_board_panel.phy, LVDS_BASE + LVDS_0_PHY_CTL);
    writel(aic_board_panel.phy, LVDS_BASE + LVDS_1_PHY_CTL);

    reg = readl(LVDS_BASE + LVDS_CTL);
    reg &= ~(LVDS_CTL_MODE_MASK | LVDS_CTL_LINK_MASK |
             LVDS_CTL_SWAP_MASK | LVDS_CTL_SYNC_MODE_MASK);
    reg |= LVDS_CTL_MODE(aic_board_panel.lvds_mode);
    reg |= LVDS_CTL_LINK(aic_board_panel.lvds_link);
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
                     aic_board_panel.hfp, aic_board_panel.hbp,
                     aic_board_panel.vfp, aic_board_panel.vbp,
                     aic_board_panel.hsw, aic_board_panel.vsw,
                     aic_board_panel.hsync_positive, aic_board_panel.vsync_positive);

    de_set_blending_size(de, disp->width, disp->height);
    de_set_ui_layer_size(de, disp->width, disp->height, 0, 0);
    de_set_ui_layer_format(de, MPP_FMT_XRGB_8888);
    de_ui_layer_set_rect(de, disp->width, disp->height, 0, 0,
                         disp->stride,
                         (u32)(uintptr_t)disp->fb[disp->active], 0);
    de_ui_layer_rect_enable(de, 0, 1);
    de_ui_bg_blending_enable(de, fb_bg_blend);
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
    de_set_ui_layer_format(de, MPP_FMT_XRGB_8888);
    de_ui_alpha_blending_enable(de, fb_alpha.value, fb_alpha.mode, !!fb_alpha.enable);
    de_ui_layer_enable(de, 1);
    de_ui_layer_set_rect(de, disp->width, disp->height, 0, 0,
                         disp->stride,
                         (u32)(uintptr_t)disp->fb[index], 0);
    de_config_update_enable(de, 1);

    memset(&fb_layers[1], 0, sizeof(fb_layers[1]));
    fb_layers[1].layer_id = AICFB_LAYER_TYPE_UI;
    fb_layers[1].enable = 1;
    fb_layers[1].buf.buf_type = MPP_PHY_ADDR;
    fb_layers[1].buf.format = MPP_FMT_XRGB_8888;
    fb_layers[1].buf.size.width = disp->width;
    fb_layers[1].buf.size.height = disp->height;
    fb_layers[1].buf.stride[0] = disp->stride;
    fb_layers[1].buf.phy_addr[0] = (uintptr_t)disp->fb[index];
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

static int disp_wait_vsync(int timeout_ms)
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

int aic_disp_wait_vsync(int timeout_ms)
{
    int ret;
    if (!g_disp.inited) return -RT_ERROR;
    rt_mutex_take(&disp_control_mutex, RT_WAITING_FOREVER);
    ret = disp_wait_vsync(timeout_ms);
    rt_mutex_release(&disp_control_mutex);
    return ret;
}

int aic_disp_pan(int index, int wait_vsync)
{
    struct aic_disp *disp = &g_disp;
    int ret;
    if (!disp->inited) return -RT_ERROR;
    if (index < 0 || index >= (int)disp->fb_count) return -RT_EINVAL;
    rt_mutex_take(&disp_control_mutex, RT_WAITING_FOREVER);
    if (!disp->power_on) ret = -RT_EIO;
    else if (layer_owner[1] != &disp->dev) ret = -RT_EBUSY;
    else
    {
        aic_disp_cache_clean(index);
        ret = disp_apply_buffer(disp, index);
        if (!ret && wait_vsync) ret = disp_wait_vsync(DISP_VSYNC_TIMEOUT_MS);
        fb_layers[1].buf.phy_addr[0] = (uintptr_t)disp->fb[disp->active];
    }
    rt_mutex_release(&disp_control_mutex);
    return ret;
}

static int disp_colorbar(int enable)
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

int aic_disp_colorbar(int enable)
{
    int ret;
    if (!g_disp.inited) return -RT_ERROR;
    rt_mutex_take(&disp_control_mutex, RT_WAITING_FOREVER);
    if (layer_owner[0] || layer_owner[1] != &g_disp.dev) ret = -RT_EBUSY;
    else ret = disp_colorbar(enable);
    rt_mutex_release(&disp_control_mutex);
    return ret;
}

static int disp_power(struct aic_disp *disp, int on)
{
    void *de = (void *)DE_BASE;

    if (on == disp->power_on)
        return RT_EOK;

    if (on)
    {
        int ret;

        ret = disp_panel_gpio(0);
        if (ret != RT_EOK)
            return ret;
        ret = aic_disp_clk_set(aic_board_panel.pixclock, aic_board_panel.lvds_link,
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
        disp_apply_buffer(disp, disp->active);
        aic_dcache_clean_range((uintptr_t)disp->fb[disp->active],
                               disp->fb_size);
        de_timing_enable(de, 1);

        rt_thread_mdelay(60);
        disp->power_on = RT_TRUE;
        ret = disp_panel_gpio(1);
        if (ret != RT_EOK)
        {
            disp_power(disp, 0);
            return ret;
        }
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

/* Both SDK-format and RT-Thread controls use this single display. */
static int disp_program_layer(struct aic_disp *d, struct aicfb_layer_data *l)
{
    void *de = (void *)DE_BASE;
    struct aic_buffer_layout layout;
    struct mpp_buf *b;
    u32 addr[3] = {0}, iw, ih, ow, oh, x, y, cw, ch;
    int ret;
    if (!l || l->layer_id > 1 || l->rect_id) return -RT_EINVAL;
    if (!d->power_on) return -RT_EIO;
    if (!l->enable) {
        de_config_update_enable(de, 0);
        if (l->layer_id == AICFB_LAYER_TYPE_VIDEO) de_video_layer_enable(de, 0);
        else de_ui_layer_enable(de, 0);
        de_config_update_enable(de, 1);
        return 0;
    }
    b = &l->buf;
    ret = aic_buffer_layout(b, &layout);
    if (ret) return ret;
    iw = b->crop_en ? b->crop.width : b->size.width;
    ih = b->crop_en ? b->crop.height : b->size.height;
    x = b->crop_en ? b->crop.x : 0;
    y = b->crop_en ? b->crop.y : 0;
    ow = l->scale_size.width ? l->scale_size.width : iw;
    oh = l->scale_size.height ? l->scale_size.height : ih;
    if (l->pos.x < 0 || l->pos.y < 0 || ow == 0 || oh == 0 ||
        ow > d->width || oh > d->height || l->pos.x > d->width - ow || l->pos.y > d->height - oh)
        return -RT_EINVAL;
    for (unsigned i=0; i<layout.planes; i++) {
        if (b->stride[i] > (l->layer_id == AICFB_LAYER_TYPE_UI ? 32767 : 8191))
            return -RT_EINVAL;
        addr[i] = b->phy_addr[i];
    }
    if (layout.bytes_per_pixel) addr[0] += y*b->stride[0] + x*layout.bytes_per_pixel;
    else {
        addr[0] += y*b->stride[0] + x;
        for (unsigned i=1; i<layout.planes; i++)
            addr[i] += (y/layout.sub_y)*b->stride[i] + x/(layout.planes == 2 ? 1 : layout.sub_x);
    }
    if (l->layer_id == AICFB_LAYER_TYPE_UI) {
        if (b->format > MPP_FMT_BGRA_4444 || iw != ow || ih != oh) return -RT_EINVAL;
        for (unsigned int i = 0; i < layout.planes; i++)
            aic_dcache_clean_range(b->phy_addr[i], layout.length[i]);
        de_config_update_enable(de, 0);
        de_set_ui_layer_format(de, b->format);
        de_ui_layer_set_rect(de, iw, ih, l->pos.x, l->pos.y, b->stride[0], addr[0], 0);
        de_ui_layer_rect_enable(de, 0, 1); de_ui_layer_enable(de, 1);
    } else {
        int scaled = b->format >= MPP_FMT_YUV420P && b->format != MPP_FMT_YUV444P;
        if (iw > 2048 || iw < 2 || ih < 2 || ow < 2 || oh < 2) return -RT_EINVAL;
        /* The SDK v1.0 RGB and YUV444 paths bypass scaler0. */
        if (!scaled && (iw != ow || ih != oh)) return -RT_EINVAL;
        cw = iw/layout.sub_x; ch = ih/layout.sub_y;
        for (unsigned int i = 0; i < layout.planes; i++)
            aic_dcache_clean_range(b->phy_addr[i], layout.length[i]);
        de_config_update_enable(de, 0);
        de_set_video_layer_info(de, iw, ih, b->format, b->stride[0], b->stride[1],
            addr[0], layout.planes > 1 ? addr[1] : 0, layout.planes > 2 ? addr[2] : 0, l->pos.x, l->pos.y);
        de_set_video_layer_tile_offset(de, 0, 0, 0, 0);
        if (scaled)
        {
            unsigned int step;
            unsigned int coeff;
            de_set_scaler0_channel(de, iw, ih, ow, oh, 0);
            if (b->format != MPP_FMT_YUV400)
                de_set_scaler0_channel(de, cw, ch, ow, oh, 1);
            step = (ch << 16) / oh;
            coeff = step <= 0x18000 ? 0 : step <= 0x20000 ? 1 : step <= 0x2c000 ? 2 : 3;
            de_scaler0_active_handle(de, coeff);
            de_set_hsbc_with_csc_coefs(de, MPP_BUF_COLOR_SPACE_GET(b->flags), 50, 50, 50, 50);
        }
        de_scaler0_enable(de, scaled);
        de_video_layer_enable(de, 1);
    }
    aicos_dma_sync();
    de_config_update_enable(de, 1);
    return 0;
}
/* Successful commit is a scanout lifetime boundary, not just an MMIO write.
 * On a failed VSync retain BOTH old and submitted buffers; only a successful
 * disable/release permits reclamation. No replacement is accepted meanwhile. */
static int disp_layer_commit(struct aic_disp *d, struct aicfb_layer_data *layer)
{
    int ret;
    unsigned int id = layer->layer_id;
    if (id > 1 || (layer_uncertain[id] && layer->enable)) return -RT_EBUSY;
    ret = disp_program_layer(d, layer);
    if (ret) return ret;
    ret = aic_disp_wait_vsync(DISP_VSYNC_TIMEOUT_MS);
    if (ret)
    {
        layer_uncertain[id] = RT_TRUE;
        return ret;
    }
    layer_uncertain[id] = RT_FALSE;
    fb_layers[id] = *layer;
    return RT_EOK;
}

int aic_disp_claim_layer(unsigned int id, const void *owner)
{
    int ret = RT_EOK;
    if (!g_disp.inited || id > 1 || !owner) return -RT_EINVAL;
    rt_mutex_take(&disp_control_mutex, RT_WAITING_FOREVER);
    if (layer_owner[id] && layer_owner[id] != owner) ret = -RT_EBUSY;
    else
    {
        if (!layer_owner[id] && id == AICFB_LAYER_TYPE_UI && owner == &g_disp.dev)
        {
            fb_alpha.enable = 0;
            fb_alpha.mode = AICFB_PIXEL_ALPHA_MODE;
            fb_alpha.value = 255;
        }
        layer_owner[id] = owner;
    }
    rt_mutex_release(&disp_control_mutex);
    return ret;
}

int aic_disp_update_layer(const void *owner, const struct aicfb_layer_data *layer)
{
    struct aicfb_layer_data copy;
    int ret;
    if (!g_disp.inited || !layer || !owner) return -RT_EINVAL;
    copy = *layer;
    if (copy.layer_id > 1) return -RT_EINVAL;
    rt_mutex_take(&disp_control_mutex, RT_WAITING_FOREVER);
    if (layer_owner[copy.layer_id] != owner) ret = -RT_EBUSY;
    else ret = disp_layer_commit(&g_disp, &copy);
    rt_mutex_release(&disp_control_mutex);
    return ret;
}

int aic_disp_release_layer(unsigned int id, const void *owner)
{
    struct aicfb_layer_data layer = {0};
    int ret;
    if (!g_disp.inited || id > 1 || !owner) return -RT_EINVAL;
    rt_mutex_take(&disp_control_mutex, RT_WAITING_FOREVER);
    if (layer_owner[id] != owner) ret = -RT_EBUSY;
    else
    {
        layer.layer_id = id;
        ret = disp_layer_commit(&g_disp, &layer);
        if (!ret) layer_owner[id] = RT_NULL;
    }
    rt_mutex_release(&disp_control_mutex);
    return ret;
}

int aic_disp_update_alpha(const void *owner, const struct aicfb_alpha_config *alpha)
{
    struct aicfb_alpha_config copy;
    int ret;
    if (!g_disp.inited || !owner || !alpha) return -RT_EINVAL;
    copy = *alpha;
    if (copy.layer_id != AICFB_LAYER_TYPE_UI || copy.mode > 2 || copy.value > 255)
        return -RT_EINVAL;
    rt_mutex_take(&disp_control_mutex, RT_WAITING_FOREVER);
    if (layer_owner[1] != owner || layer_uncertain[1]) ret = -RT_EBUSY;
    else if (!g_disp.power_on) ret = -RT_EIO;
    else
    {
        de_config_update_enable((void *)DE_BASE, 0);
        de_ui_alpha_blending_enable((void *)DE_BASE, copy.value, copy.mode, !!copy.enable);
        de_config_update_enable((void *)DE_BASE, 1);
        ret = disp_wait_vsync(DISP_VSYNC_TIMEOUT_MS);
        if (!ret) fb_alpha = copy;
        else layer_uncertain[1] = RT_TRUE;
    }
    rt_mutex_release(&disp_control_mutex);
    return ret;
}

/* Like alpha, background blending belongs to the current UI owner. A
 * timed-out register latch retains the ownership/buffer uncertainty guard. */
int aic_disp_update_bg_blend(const void *owner, unsigned int enable)
{
    int ret;
    if (!g_disp.inited || !owner || enable > 1) return -RT_EINVAL;
    rt_mutex_take(&disp_control_mutex, RT_WAITING_FOREVER);
    if (layer_owner[1] != owner || layer_uncertain[1]) ret = -RT_EBUSY;
    else if (!g_disp.power_on) ret = -RT_EIO;
    else
    {
        de_config_update_enable((void *)DE_BASE, 0);
        de_ui_bg_blending_enable((void *)DE_BASE, enable);
        de_config_update_enable((void *)DE_BASE, 1);
        ret = disp_wait_vsync(DISP_VSYNC_TIMEOUT_MS);
        if (!ret) fb_bg_blend = enable;
        else layer_uncertain[1] = RT_TRUE;
    }
    rt_mutex_release(&disp_control_mutex);
    return ret;
}

static int disp_fb_control(struct aic_disp *d, int cmd, void *arg)
{
    switch (cmd) {
    case AICFB_GET_SCREENINFO: {
        struct aicfb_screeninfo *i = arg;
        if (!i) return -RT_EINVAL;
        i->format = MPP_FMT_XRGB_8888; i->bits_per_pixel = 32; i->stride = d->stride;
        i->width = d->width; i->height = d->height; i->framebuffer = d->fb[d->active];
        i->smem_len = d->fb_size*d->fb_count;
        return 0;
    }
    case AICFB_GET_LAYER_CONFIG: {
        struct aicfb_layer_data *l = arg;
        if (!l || l->layer_id > 1 || l->rect_id) return -RT_EINVAL;
        *l = fb_layers[l->layer_id]; return 0;
    }
    case AICFB_UPDATE_LAYER_CONFIG:
        /* Raw legacy commands carry no owner token. They may update only
         * the reserved framebuffer UI, never steal an external video/UI. */
        if (!arg) return -RT_EINVAL;
        {
            struct aicfb_layer_data layer = *(struct aicfb_layer_data *)arg;
            if (layer.layer_id != AICFB_LAYER_TYPE_UI ||
                layer_owner[1] != &d->dev) return -RT_EBUSY;
            if (layer.enable && (layer.buf.size.width != d->width ||
                layer.buf.size.height != d->height || layer.buf.stride[0] != d->stride ||
                (layer.buf.format != MPP_FMT_XRGB_8888 && layer.buf.format != MPP_FMT_ARGB_8888)))
                return -RT_EINVAL;
            if (layer.enable && (layer.buf.phy_addr[0] != (uintptr_t)d->fb[0] &&
                (d->fb_count < 2 || layer.buf.phy_addr[0] != (uintptr_t)d->fb[1])))
                return -RT_EINVAL;
            return disp_layer_commit(d, &layer);
        }
    case AICFB_WAIT_FOR_VSYNC: return aic_disp_wait_vsync(DISP_VSYNC_TIMEOUT_MS);
    case AICFB_GET_BG_BLEND_UI_CONFIG:
        if (!arg) return -RT_EINVAL;
        *(unsigned int *)arg = fb_bg_blend;
        return RT_EOK;
    case AICFB_UPDATE_BG_BLEND_UI_CONFIG:
        if (!arg) return -RT_EINVAL;
        return aic_disp_update_bg_blend(&d->dev, *(unsigned int *)arg);
    case AICFB_GET_ALPHA_CONFIG:
        if (!arg || ((struct aicfb_alpha_config *)arg)->layer_id != AICFB_LAYER_TYPE_UI) return -RT_EINVAL;
        *(struct aicfb_alpha_config *)arg = fb_alpha; return 0;
    case AICFB_UPDATE_ALPHA_CONFIG:
        return aic_disp_update_alpha(&d->dev, arg);
    case AICFB_POWERON: return disp_power(d, 1);
    case AICFB_POWEROFF:
        if (layer_owner[0] || layer_owner[1] != &d->dev) return -RT_EBUSY;
        return disp_power(d, 0);
    default: return -RT_ENOSYS;
    }
}

static rt_err_t disp_control_locked(rt_device_t dev, int cmd, void *args)
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
        if (layer_owner[0] || layer_owner[1] != &disp->dev) return -RT_EBUSY;
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
        return disp_fb_control(disp, cmd, args);
    }
}
static rt_err_t disp_control(rt_device_t dev, int cmd, void *args)
{
    rt_err_t ret;
    rt_mutex_take(&disp_control_mutex, RT_WAITING_FOREVER);
    ret = disp_control_locked(dev, cmd, args);
    rt_mutex_release(&disp_control_mutex);
    return ret;
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
    rt_uint32_t address;

    if (disp->inited)
        return RT_EOK;

    if (!aic_board_panel.width || !aic_board_panel.height ||
        aic_board_panel.width > 4096 || aic_board_panel.height > 4096 ||
        !aic_board_panel.fb_count || aic_board_panel.fb_count > 2 ||
        aic_board_panel.ui_bg_blend > 1)
        return -RT_EINVAL;

    if ((size_t)(__fb_end - __fb_start) <
        (size_t)aic_board_panel.width * aic_board_panel.height * 4)
    {
        rt_kprintf("lcd: framebuffer reserve too small\n");
        return -RT_ENOMEM;
    }

    reserved = (size_t)(__fb_end - __fb_start);
    disp->width = aic_board_panel.width;
    disp->height = aic_board_panel.height;
    disp->bpp = DISP_FB_BPP;
    disp->stride = DISP_ALIGN_64(disp->width * (disp->bpp / 8));
    disp->fb_size = DISP_ALIGN_64(disp->stride * disp->height);
    disp->fb_count = aic_board_panel.fb_count;
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

    if (aic_dma_address((uintptr_t)disp->fb[0], disp->fb_size * disp->fb_count, &address))
        return -RT_EINVAL;

    memset(disp->fb[0], 0, disp->fb_size * disp->fb_count);
    aic_dcache_clean_invalidate_range((uintptr_t)disp->fb[0],
                                      disp->fb_size * disp->fb_count);

    rt_completion_init(&disp->vsync);

    fb_bg_blend = aic_board_panel.ui_bg_blend;
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

    if (rt_mutex_init(&disp_control_mutex, "dispio", RT_IPC_FLAG_PRIO))
        return -RT_ERROR;
    if (rt_device_register(&disp->dev, "lcd", RT_DEVICE_FLAG_RDWR) != RT_EOK)
    {
        rt_kprintf("lcd: device register failed\n");
        rt_mutex_detach(&disp_control_mutex);
        disp_power(disp, 0);
        return -RT_ERROR;
    }

    layer_owner[AICFB_LAYER_TYPE_UI] = &disp->dev;
    fb_layers[0].layer_id = AICFB_LAYER_TYPE_VIDEO;
    fb_layers[1].layer_id = AICFB_LAYER_TYPE_UI;
    fb_layers[1].enable = 1;
    fb_layers[1].buf.buf_type = MPP_PHY_ADDR;
    fb_layers[1].buf.format = MPP_FMT_XRGB_8888;
    fb_layers[1].buf.size.width = disp->width;
    fb_layers[1].buf.size.height = disp->height;
    fb_layers[1].buf.stride[0] = disp->stride;
    fb_layers[1].buf.phy_addr[0] = (uintptr_t)disp->fb[disp->active];
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
