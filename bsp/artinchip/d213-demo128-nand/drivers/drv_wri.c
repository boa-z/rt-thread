/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D21x WRI (write restart info) reboot reason.
 *
 * D21x uses WRI V1.0.  In this version the boot information register is not
 * part of the WRI block; Luban-Lite keeps it in the always-on RTC block
 * (bsp/artinchip/include/hal/hal_wri.h, AIC_WRI_DRV_V10 path):
 *
 *   RTC_BASE + 0x100  boot info, reboot reason in bits [7:4]
 *   RTC_BASE + 0x0FC  write enable, 0xAC unlocks, 0x00 locks
 *
 * The reason values follow bsp/common/include/aic_reboot_reason.h.  The BROM
 * converts REBOOT_REASON_UPGRADE into its USB/UART upgrade loop, and the
 * vendor bootloader converts REBOOT_REASON_BL_UPGRADE into
 * "aicupg usb|uart 0" (application/baremetal/bootloader/cmd/aicupg_detect.c).
 *
 * The WRI/RTC block is in the always-on domain, so a watchdog warm reset
 * preserves the latched reason.  This driver only latches the request; it
 * never touches flash, eFuse or the bootloader.
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "aic_compat.h"

#define WRI_BASE                    0x1900F000UL
#define WRI_VERSION                 (WRI_BASE + 0xFFC)

#define WRI_RTC_BOOT_INFO           (RTC_BASE + 0x100)
#define WRI_RTC_WR_EN               (RTC_BASE + 0x0FC)
#define WRI_WR_EN_KEY               0xAC

#define WRI_REBOOT_REASON_MASK      (0xF << 4)
#define WRI_REBOOT_REASON_SHIFT     4

enum aic_reboot_reason
{
    REBOOT_REASON_COLD = 0,
    REBOOT_REASON_CMD_REBOOT,
    REBOOT_REASON_CMD_SHUTDOWN,
    REBOOT_REASON_SUSPEND,
    REBOOT_REASON_UPGRADE,          /* BROM USB/UART upgrade mode */
    REBOOT_REASON_BL_UPGRADE,       /* vendor Bootloader upgrade mode */
    REBOOT_REASON_BL_HID_UPGRADE,
};

static rt_uint8_t wri_boot_info_read(void)
{
    return readb(WRI_RTC_BOOT_INFO);
}

static void wri_boot_info_write(rt_uint8_t value)
{
    writeb(WRI_WR_EN_KEY, WRI_RTC_WR_EN);
    writeb(value, WRI_RTC_BOOT_INFO);
    writeb(0, WRI_RTC_WR_EN);
}

static rt_uint8_t wri_reason_get(void)
{
    return (rt_uint8_t)((wri_boot_info_read() & WRI_REBOOT_REASON_MASK)
                        >> WRI_REBOOT_REASON_SHIFT);
}

static void wri_reboot_reason_clear(void)
{
    rt_uint8_t info = wri_boot_info_read();

    wri_boot_info_write((rt_uint8_t)(info & ~WRI_REBOOT_REASON_MASK));
}

/*
 * Latch a reboot reason and verify it.  An already latched reason is never
 * overwritten: the BROM or the bootloader clears it on the next boot, so a
 * non-zero value means a previous request did not reach the reset.
 */
static rt_err_t wri_reboot_reason_set(rt_uint8_t reason)
{
    rt_uint8_t cur = wri_boot_info_read();
    rt_uint8_t val;

    if (wri_reason_get() == reason)
        return RT_EOK;
    if (cur & WRI_REBOOT_REASON_MASK)
        return -RT_EBUSY;

    val = (rt_uint8_t)((cur & ~WRI_REBOOT_REASON_MASK) |
          ((reason << WRI_REBOOT_REASON_SHIFT) & WRI_REBOOT_REASON_MASK));
    wri_boot_info_write(val);

    if (wri_boot_info_read() != val)
        return -RT_ERROR;

    return RT_EOK;
}

static int rt_hw_wri_init(void)
{
    rt_uint8_t reason = wri_reason_get();

    if (reason != REBOOT_REASON_COLD)
    {
        wri_reboot_reason_clear();
        rt_kprintf("wri: cleared stale reboot reason %d\n", reason);
    }

    rt_kprintf("wri: v%08x boot_info=0x%02x\n",
               (unsigned int)readl(WRI_VERSION), wri_boot_info_read());

    return 0;
}
INIT_ENV_EXPORT(rt_hw_wri_init);

#ifdef RT_USING_FINSH
#include <finsh.h>

static void cmd_aicupg(int argc, char **argv)
{
    rt_uint8_t reason = REBOOT_REASON_UPGRADE;
    const char *mode = "BROM";
    rt_device_t wdt;
    rt_uint32_t timeout = 1;
    rt_err_t err;

    if (argc == 2)
    {
        if (rt_strcmp(argv[1], "gotobl") != 0)
        {
            rt_kprintf("Usage: aicupg [gotobl]\n");
            return;
        }
        reason = REBOOT_REASON_BL_UPGRADE;
        mode = "bootloader";
    }
    else if (argc > 2)
    {
        rt_kprintf("Usage: aicupg [gotobl]\n");
        return;
    }

    wdt = rt_device_find("wdt");
    if (wdt == RT_NULL)
    {
        rt_kprintf("aicupg: no wdt device, not latching the %s reason\n", mode);
        return;
    }

    err = wri_reboot_reason_set(reason);
    if (err != RT_EOK)
    {
        rt_kprintf("aicupg: cannot latch reason %d (%s), error %d, boot_info=0x%02x\n",
                   reason, mode, err, wri_boot_info_read());
        return;
    }

    rt_kprintf("aicupg: %s upgrade reason %d latched and verified, resetting\n",
               mode, reason);

    err = rt_device_open(wdt, RT_DEVICE_OFLAG_RDWR);
    if (err == RT_EOK)
        err = rt_device_control(wdt, RT_DEVICE_CTRL_WDT_SET_TIMEOUT, &timeout);
    if (err == RT_EOK)
        err = rt_device_control(wdt, RT_DEVICE_CTRL_WDT_START, RT_NULL);

    if (err != RT_EOK)
    {
        wri_reboot_reason_clear();
        rt_kprintf("aicupg: watchdog reset failed (%d), request cleared\n", err);
        return;
    }

    /* The watchdog warm reset keeps the always-on WRI/RTC value. */
    while (1)
        rt_thread_mdelay(100);
}
MSH_CMD_EXPORT_ALIAS(cmd_aicupg, aicupg, reboot into the ArtInChip upgrade mode);
#endif
