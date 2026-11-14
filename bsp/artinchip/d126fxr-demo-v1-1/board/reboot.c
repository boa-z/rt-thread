/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * D12p warm reset and upgrade-mode entry.
 *
 * The D12p bootloader (and BROM) read the software reboot reason from
 * the always-on WRI block, then decide whether to stay in upgrade mode.
 * The application writes the reason, then lets the WDT reset the SoC.
 * The WDT is programmed exactly like the vendor reboot path: 32 kHz
 * counter, zero reset threshold, counter enabled.  No flash, eFuse or
 * security state is touched here.
 *
 * This is the minimum needed by the HIL to enter the D12p upgrade flow
 * from the console; it is not a general watchdog driver.
 */

#include <rtthread.h>
#include <stdint.h>
#include <string.h>

#ifdef RT_USING_FINSH
#include <finsh.h>
#endif

#include "board.h"

#define AIC_WRI_BASE                0x1900F000UL
#define AIC_WRI_RST_FLAG            (AIC_WRI_BASE + 0x000UL)
#define AIC_WRI_BOOT_INFO           (AIC_WRI_BASE + 0x100UL)
#define AIC_WRI_REASON_SHIFT        4
#define AIC_WRI_REASON_MASK         0xF0UL

/* WRI V16 reason values, from Luban-Lite aic_reboot_reason.h. */
#define AIC_REBOOT_REASON_CMD_REBOOT    1   /* normal warm reset */
#define AIC_REBOOT_REASON_UPGRADE       4   /* BROM upgrade mode */
#define AIC_REBOOT_REASON_BL_UPGRADE    5   /* Bootloader upgrade mode */
#define AIC_REBOOT_REASON_BL_HID_UPGRADE 6  /* Bootloader HID upgrade mode */

#define AIC_CMU_WDT_CLK_REG         0x020CUL
#define AIC_CMU_WR_AUTH_REG         0x0FE8UL
#define AIC_CMU_WR_KEY              0xA1CUL
#define AIC_CMU_WR_KEY_SHIFT        20
#define AIC_CMU_WR_AUTH_BIT         16
#define AIC_CMU_WDT_GATES           ((1UL << 16) | (1UL << 20))
#define AIC_CMU_MOD_RSTN            (1UL << 24)

#define AIC_WDT_BASE                0x19000000UL
#define AIC_WDT_CTL                 0x000UL
#define AIC_WDT_CLR_THD0            0x040UL
#define AIC_WDT_RST_THD0            0x048UL
#define AIC_WDT_OP                  0x0E8UL
#define AIC_WDT_CNT_EN              (1UL << 0)
#define AIC_WDT_OP_CNT_CLR_CMD0     0xA1C55555UL
#define AIC_WDT_OP_CNT_CLR_CMD1     0xA1CAAAAAUL

static inline uint32_t d12p_readl(uint32_t addr)
{
    return *(volatile uint32_t *)(uintptr_t)addr;
}

static inline void d12p_writel(uint32_t value, uint32_t addr)
{
    *(volatile uint32_t *)(uintptr_t)addr = value;
}

static int d12p_cmu_auth(uint32_t reg)
{
    uint32_t i;

    d12p_writel((AIC_CMU_WR_KEY << AIC_CMU_WR_KEY_SHIFT) | reg,
                AIC_CMU_BASE + AIC_CMU_WR_AUTH_REG);
    for (i = 0; i < 10000; i++)
    {
        __asm__ volatile("nop");
    }

    return (d12p_readl(AIC_CMU_BASE + AIC_CMU_WR_AUTH_REG) &
            (1UL << AIC_CMU_WR_AUTH_BIT)) ? 0 : -1;
}

static void d12p_cmu_wdt_write(uint32_t value)
{
    if (d12p_cmu_auth(AIC_CMU_WDT_CLK_REG) == 0)
    {
        d12p_writel(value, AIC_CMU_BASE + AIC_CMU_WDT_CLK_REG);
    }
}

static void aic_board_clear_reboot_reason(void)
{
    uint32_t flags;
    uint32_t info;

    /* Warm reset flags are write-1-to-clear. */
    flags = d12p_readl(AIC_WRI_RST_FLAG);
    d12p_writel(flags, AIC_WRI_RST_FLAG);

    info = d12p_readl(AIC_WRI_BOOT_INFO);
    if (info & AIC_WRI_REASON_MASK)
    {
        d12p_writel(info & ~AIC_WRI_REASON_MASK, AIC_WRI_BOOT_INFO);
    }
}

/*
 * The vendor WRI driver consumes and clears the software reboot reason at
 * init.  Without this, a reason latched by the flasher's post-burn reset
 * (REBOOT_REASON_CMD_REBOOT) stays in the always-on block and a later
 * "aicupg" would refuse to overwrite it.
 */
INIT_BOARD_EXPORT(aic_board_clear_reboot_reason);

static int aic_board_set_reboot_reason(uint32_t reason)
{
    uint32_t cur;
    uint32_t want;
    uint32_t mask;

    mask = (reason << AIC_WRI_REASON_SHIFT) & AIC_WRI_REASON_MASK;
    cur = d12p_readl(AIC_WRI_BOOT_INFO);

    /*
     * A reason can only reach this point if something latched it after
     * the board init, or if the image predates the init clear.  Drop the
     * stale value instead of blocking the explicit user request.
     */
    if (cur & AIC_WRI_REASON_MASK)
    {
        rt_kprintf("aicupg: clearing stale WRI reason 0x%02x\n",
                   (unsigned)(cur & AIC_WRI_REASON_MASK));
        d12p_writel(cur & ~AIC_WRI_REASON_MASK, AIC_WRI_BOOT_INFO);
        cur = d12p_readl(AIC_WRI_BOOT_INFO);
    }

    want = (cur & ~AIC_WRI_REASON_MASK) | mask;
    d12p_writel(want, AIC_WRI_BOOT_INFO);
    cur = d12p_readl(AIC_WRI_BOOT_INFO);
    return ((cur & AIC_WRI_REASON_MASK) == mask) ? 0 : -1;
}

static void aic_board_wdt_reset(void)
{
    uint32_t val;

    /* Enable the WDT module/APB gates and release its reset. */
    val = d12p_readl(AIC_CMU_BASE + AIC_CMU_WDT_CLK_REG);
    val |= AIC_CMU_WDT_GATES | AIC_CMU_MOD_RSTN;
    d12p_cmu_wdt_write(val);

    /* 32 kHz counter, reset threshold 0 -> reset as soon as enabled. */
    d12p_writel(0, AIC_WDT_BASE + AIC_WDT_RST_THD0);
    d12p_writel(0, AIC_WDT_BASE + AIC_WDT_CLR_THD0);
    d12p_writel(AIC_WDT_OP_CNT_CLR_CMD0, AIC_WDT_BASE + AIC_WDT_OP);
    d12p_writel(AIC_WDT_OP_CNT_CLR_CMD1, AIC_WDT_BASE + AIC_WDT_OP);
    d12p_writel(AIC_WDT_CNT_EN, AIC_WDT_BASE + AIC_WDT_CTL);
}

#ifdef RT_USING_FINSH
static int cmd_aicupg(int argc, char **argv)
{
    uint32_t reason = AIC_REBOOT_REASON_UPGRADE;
    const char *target = "brom";

    if (argc > 2)
    {
        rt_kprintf("usage: aicupg [gotobl|hid]\n");
        return -1;
    }
    if (argc == 2)
    {
        if (!strcmp(argv[1], "gotobl"))
        {
            reason = AIC_REBOOT_REASON_BL_UPGRADE;
            target = "bootloader";
        }
        else if (!strcmp(argv[1], "hid"))
        {
            reason = AIC_REBOOT_REASON_BL_HID_UPGRADE;
            target = "bootloader-hid";
        }
        else
        {
            rt_kprintf("usage: aicupg [gotobl|hid]\n");
            return -1;
        }
    }

    if (aic_board_set_reboot_reason(reason) != 0)
    {
        rt_kprintf("aicupg: FAIL, reboot reason %u was not latched\n",
                   (unsigned)reason);
        return -1;
    }

    rt_kprintf("aicupg: reboot reason %u (%s) latched and verified, "
               "resetting\n", (unsigned)reason, target);
    rt_thread_mdelay(50);
    aic_board_wdt_reset();

    rt_kprintf("aicupg: FAIL, watchdog reset did not fire\n");
    return -1;
}
MSH_CMD_EXPORT_ALIAS(cmd_aicupg, aicupg,
                     "Reboot to the D12p upgrade mode");

static int cmd_reboot(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (aic_board_set_reboot_reason(AIC_REBOOT_REASON_CMD_REBOOT) != 0)
    {
        rt_kprintf("reboot: FAIL, WRI reason was not latched\n");
        return -1;
    }

    rt_kprintf("reboot: WRI reason 1 (command reboot) latched and verified, "
               "resetting\n");
    rt_thread_mdelay(50);
    aic_board_wdt_reset();

    rt_kprintf("reboot: FAIL, watchdog reset did not fire\n");
    return -1;
}
MSH_CMD_EXPORT_ALIAS(cmd_reboot, reboot,
                     "Warm-reset the SoC through the WDT");
#endif /* RT_USING_FINSH */
