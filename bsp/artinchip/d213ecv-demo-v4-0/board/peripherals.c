/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 * D213ECV-DEMO-V4-0 wiring, panel and power sequencing.
 */
#include <rtthread.h>
#include <rtdevice.h>
#include "aic_board.h"

#ifdef BSP_USING_DISPLAY
#include "aic_hal_gpio.h"
#include "aic_hal_lvds.h"

static int panel_set_power(int on)
{
    long pin = hal_gpio_name2pin(BSP_DISPLAY_PANEL_ENABLE_GPIO);
    unsigned int group, offset;
    int level = !!on;

    if (pin < 0)
        return -RT_EINVAL;
    group = GPIO_GROUP(pin);
    offset = GPIO_GROUP_PIN(pin);
#ifdef BSP_DISPLAY_PANEL_ENABLE_LOW
    level = !level;
#endif
    hal_gpio_set_func(group, offset, 1);
    hal_gpio_set_bias_pull(group, offset, PIN_PULL_DIS);
    hal_gpio_set_drive_strength(group, offset, 3);
    /* Set the latch before enabling output. */
    if (level)
        hal_gpio_set_output(group, offset);
    else
        hal_gpio_clr_output(group, offset);
    hal_gpio_direction_output(group, offset);
    return RT_EOK;
}

const struct aic_panel_config aic_board_panel =
{
    .width = BSP_DISPLAY_WIDTH, .height = BSP_DISPLAY_HEIGHT,
    .pixclock = BSP_DISPLAY_PIXCLOCK,
    .hfp = BSP_DISPLAY_HFP, .hbp = BSP_DISPLAY_HBP, .hsw = BSP_DISPLAY_HSW,
    .vfp = BSP_DISPLAY_VFP, .vbp = BSP_DISPLAY_VBP, .vsw = BSP_DISPLAY_VSW,
    .lvds_link = BSP_DISPLAY_LVDS_LINK, .lvds_mode = BSP_DISPLAY_LVDS_MODE,
    .lanes = LVDS_LANES(LVDS_D3, LVDS_CK, LVDS_D2, LVDS_D1, LVDS_D0),
    .polarity = 0, .phy = 0xFA,
    .hsync_positive = 0, .vsync_positive = 0,
    .fb_count = BSP_DISPLAY_FB_COUNT, .set_power = panel_set_power,
#ifdef BSP_DISPLAY_UI_BG_BLEND
    .ui_bg_blend = 1,
#endif
};
#endif

#ifdef BSP_USING_TOUCH_GT911
const struct aic_touch_config aic_board_touch =
{
    .i2c_bus = BSP_TOUCH_GT911_I2C_BUS,
    .reset_pin = BSP_TOUCH_GT911_RST_PIN,
    .interrupt_pin = BSP_TOUCH_GT911_INT_PIN,
    .x_range = BSP_TOUCH_GT911_X_RANGE, .y_range = BSP_TOUCH_GT911_Y_RANGE,
};
#endif

#ifdef BSP_USING_AUDIO
const struct aic_board_audio_config aic_board_audio =
{
    .pa_pin = BSP_AUDIO_PA_GPIO, .pa_active_low = 1, .pa_settle_ms = 200,
#ifdef BSP_AUDIO_SPK0
    .speaker = 0,
#else
    .speaker = 1,
#endif
};
#endif

#ifdef BSP_USING_QSPI0
const struct aic_qspi_config aic_board_qspi0 =
{
    .input_hz = BSP_QSPI0_INPUT_HZ, .max_hz = BSP_QSPI0_FREQ,
    /* Demo128 NAND requires half-cycle RX sampling. */
    .rx_delay_half_cycles = 1,
};
#endif

#if defined(BSP_USING_SDMC1) || defined(BSP_USING_SDMC2)
#include "hal_sdmc.h"
#ifdef BSP_USING_SDMC2
static int module_power_pin(const char *name, int edge)
{
    rt_base_t pin;
    if (!name || !name[0])
        return RT_EOK;
    pin = rt_pin_get(name);
    if (pin < 0)
        return -RT_EINVAL;
    if (edge)
        rt_pin_write(pin, PIN_LOW);
    else
        rt_pin_write(pin, PIN_HIGH);
    rt_pin_mode(pin, PIN_MODE_OUTPUT);
    if (edge)
        rt_thread_mdelay(10);
    rt_pin_write(pin, PIN_HIGH);
    return RT_EOK;
}

static int sdmc2_power_on(void)
{
    int ret = module_power_pin(BSP_SDMC2_POWER_GPIO, 1);
    if (ret != RT_EOK)
        return ret;
    if (BSP_SDMC2_POWER_GPIO[0])
        rt_thread_mdelay(BSP_SDMC2_POWER_DELAY_MS);
    if (BSP_SDMC2_POWER_GPIO[0])
        rt_kprintf("sdmc2: WL_REG_ON %s driven high\n", BSP_SDMC2_POWER_GPIO);
    ret = module_power_pin(BSP_SDMC2_BT_POWER_GPIO, 0);
    if (ret == RT_EOK && BSP_SDMC2_BT_POWER_GPIO[0])
        rt_kprintf("sdmc2: BT_REG_ON %s driven high\n", BSP_SDMC2_BT_POWER_GPIO);
    return ret;
}
#endif

const struct aic_sdmc_slot *aic_board_sdmc_slot(unsigned int index)
{
#ifdef BSP_USING_SDMC1
    static const struct aic_sdmc_slot sdcard =
    {
        .block_name = "sd0", .bus_width = 4, .max_hz = SDMC_CLOCK_MAX,
        .drive_phase = 3, .sample_phase = 0,
    };
    if (index == 1)
        return &sdcard;
#endif
#ifdef BSP_USING_SDMC2
    static const struct aic_sdmc_slot sdio =
    {
        .block_name = "sd1", .bus_width = 4, .max_hz = SDMC_CLOCK_MAX,
        .is_sdio = 1, .power_on = sdmc2_power_on,
        .drive_phase = 3, .sample_phase = 0,
    };
    if (index == 2)
        return &sdio;
#endif
    return RT_NULL;
}
#endif

#if defined(BSP_SPINAND_RODATA_FS) || defined(BSP_SPINAND_LITTLEFS)
const struct aic_storage_config aic_board_storage =
{
    .rodata_partition = "rodata", .rodata_device = "nand_rodata",
    .rodata_mount = "/rodata", .lfs_partition = "nand_scratch",
    .lfs_mount = "/nand", .lfs_stress_path = "/nand/stress.bin",
};
#endif

#ifdef BSP_USING_GMAC0
#include "aic_hal_clk.h"
#include "hal_syscfg.h"

int aic_board_mac_prepare(unsigned int port)
{
    int ret;
    if (port != 0)
        return -RT_EINVAL;
    /* RTL8201F XTAL input is supplied by CLK_OUT2 on PE.10. */
    ret = hal_clk_set_freq(CLK_OUT2, 25000000);
    if (ret != 0)
        return ret;
    ret = hal_clk_enable(CLK_OUT2);
    if (ret != 0)
        return ret;
    hal_syscfg_gmac0_init();
    return RT_EOK;
}
#endif
