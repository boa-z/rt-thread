/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal D21x clock gate / reset provider for the curated vendor HAL.
 *
 * The ArtInChip bootloader has already programmed the PLL tree.  This
 * file therefore only exposes the gate and reset operations that the
 * selected HAL blocks call, plus the fixed input frequencies that are
 * established by the board configuration.  It deliberately does not
 * reimplement the vendor CMU framework.
 */

#include <stdint.h>

#include "aic_compat.h"
#include "aic_hal_clk.h"

#define CMU_REG(off)        (*(volatile u32 *)(CMU_BASE + (off)))
#define CMU_BUS_GATE        (1U << 12)
#define CMU_MOD_GATE(bit)   (1U << (bit))
#define CMU_RSTN            (1U << 13)

struct aic_clk_desc
{
    u32 offset;
    int bus_gate_bit;
    int mod_gate_bit;
    unsigned long freq;
};

static int aic_clk_lookup(u32 id, struct aic_clk_desc *desc)
{
    if (id >= CLK_UART0 && id <= CLK_UART7)
    {
        desc->offset = CLK_UART0_REG + (id - CLK_UART0) * 4;
        desc->bus_gate_bit = 12;
        desc->mod_gate_bit = 8;
        desc->freq = 48000000UL;
        return 0;
    }

    if (id >= CLK_QSPI0 && id <= CLK_QSPI3)
    {
        desc->offset = CLK_QSPI0_REG + (id - CLK_QSPI0) * 4;
        desc->bus_gate_bit = 12;
        desc->mod_gate_bit = 8;
        desc->freq = 100000000UL;
        return 0;
    }

    if (id >= CLK_I2C0 && id <= CLK_I2C3)
    {
        desc->offset = CLK_I2C0_REG + (id - CLK_I2C0) * 4;
        desc->bus_gate_bit = 12;
        desc->mod_gate_bit = -1;
        desc->freq = 24000000UL;
        return 0;
    }

    switch (id)
    {
    case CLK_CAN0:
        desc->offset = CLK_CAN0_REG;
        desc->bus_gate_bit = 12;
        desc->mod_gate_bit = -1;
        desc->freq = 24000000UL;
        return 0;
    case CLK_CAN1:
        desc->offset = CLK_CAN1_REG;
        desc->bus_gate_bit = 12;
        desc->mod_gate_bit = -1;
        desc->freq = 24000000UL;
        return 0;
    case CLK_PWM:
        desc->offset = CLK_PWM_REG;
        desc->bus_gate_bit = 12;
        desc->mod_gate_bit = 8;
        desc->freq = 48000000UL;
        return 0;
    case CLK_DE:
        desc->offset = CLK_DE_REG;
        desc->bus_gate_bit = 12;
        desc->mod_gate_bit = 8;
        desc->freq = 200000000UL;
        return 0;
    case CLK_GE:
        desc->offset = CLK_GE_REG;
        desc->bus_gate_bit = 12;
        desc->mod_gate_bit = 8;
        desc->freq = 200000000UL;
        return 0;
    case CLK_LVDS:
        desc->offset = CLK_LVDS_REG;
        desc->bus_gate_bit = 12;
        desc->mod_gate_bit = 8;
        desc->freq = 0;
        return 0;
    case CLK_WDT:
        desc->offset = CLK_WDT_REG;
        desc->bus_gate_bit = 12;
        desc->mod_gate_bit = 8;
        desc->freq = 32768UL;
        return 0;
    case CLK_RTC:
        desc->offset = CLK_RTC_REG;
        desc->bus_gate_bit = 12;
        desc->mod_gate_bit = -1;
        desc->freq = 32768UL;
        return 0;
    case CLK_GPIO:
        desc->offset = CLK_GPIO_REG;
        desc->bus_gate_bit = 12;
        desc->mod_gate_bit = -1;
        desc->freq = 100000000UL;
        return 0;
    case CLK_GTC:
        desc->offset = CLK_GTC_REG;
        desc->bus_gate_bit = 12;
        desc->mod_gate_bit = -1;
        desc->freq = 24000000UL;
        return 0;
    default:
        break;
    }

    return -EINVAL;
}

int hal_clk_enable(uint32_t clk_id)
{
    struct aic_clk_desc desc;
    u32 value;

    /* Derived display clocks have no gate of their own. */
    if (clk_id == CLK_PIX || clk_id == CLK_SCLK || clk_id == CLK_PLL_FRA2)
        return 0;

    if (aic_clk_lookup(clk_id, &desc) < 0)
        return -EINVAL;

    value = CMU_REG(desc.offset);
    if (desc.bus_gate_bit >= 0)
        value |= CMU_MOD_GATE(desc.bus_gate_bit);
    if (desc.mod_gate_bit >= 0)
        value |= CMU_MOD_GATE(desc.mod_gate_bit);
    CMU_REG(desc.offset) = value;

    return 0;
}

int hal_clk_disable(uint32_t clk_id)
{
    struct aic_clk_desc desc;
    u32 value;

    if (clk_id == CLK_PIX || clk_id == CLK_SCLK || clk_id == CLK_PLL_FRA2)
        return 0;

    if (aic_clk_lookup(clk_id, &desc) < 0)
        return -EINVAL;

    value = CMU_REG(desc.offset);
    if (desc.mod_gate_bit >= 0)
        value &= ~CMU_MOD_GATE(desc.mod_gate_bit);
    if (desc.bus_gate_bit >= 0)
        value &= ~CMU_MOD_GATE(desc.bus_gate_bit);
    CMU_REG(desc.offset) = value;

    return 0;
}

int hal_clk_is_enabled(uint32_t clk_id)
{
    struct aic_clk_desc desc;

    if (clk_id == CLK_PIX || clk_id == CLK_SCLK || clk_id == CLK_PLL_FRA2)
        return 1;

    if (aic_clk_lookup(clk_id, &desc) < 0)
        return 0;

    return !!(CMU_REG(desc.offset) & CMU_BUS_GATE);
}

int hal_clk_enable_deassertrst(uint32_t clk_id)
{
    struct aic_clk_desc desc;
    u32 value;

    if (aic_clk_lookup(clk_id, &desc) < 0)
        return -EINVAL;

    value = CMU_REG(desc.offset);
    if (desc.bus_gate_bit >= 0)
        value |= CMU_MOD_GATE(desc.bus_gate_bit);
    if (desc.mod_gate_bit >= 0)
        value |= CMU_MOD_GATE(desc.mod_gate_bit);
    CMU_REG(desc.offset) = value;

    udelay(30);

    value = CMU_REG(desc.offset);
    value |= CMU_RSTN;
    CMU_REG(desc.offset) = value;

    udelay(30);

    return 0;
}

int hal_clk_disable_assertrst(uint32_t clk_id)
{
    struct aic_clk_desc desc;

    if (aic_clk_lookup(clk_id, &desc) < 0)
        return -EINVAL;

    CMU_REG(desc.offset) &= ~CMU_RSTN;
    return 0;
}

int hal_clk_enable_deassertrst_iter(uint32_t clk_id)
{
    return hal_clk_enable_deassertrst(clk_id);
}

int hal_clk_enable_iter(uint32_t clk_id)
{
    return hal_clk_enable(clk_id);
}

unsigned long hal_clk_get_freq(uint32_t clk_id)
{
    struct aic_clk_desc desc;

    if (aic_clk_lookup(clk_id, &desc) < 0)
        return 24000000UL;

    return desc.freq;
}

unsigned int hal_clk_get_parent(uint32_t clk_id)
{
    (void)clk_id;
    return 0;
}

int hal_clk_set_parent(uint32_t clk_id, unsigned int parent_clk_id)
{
    /*
     * The bootloader has already selected the parent.  Accept the request
     * so that HAL init paths that only "confirm" the parent can proceed.
     */
    (void)clk_id;
    (void)parent_clk_id;
    return 0;
}

int hal_clk_set_rate(uint32_t clk_id, unsigned long rate,
                     unsigned long parent_rate)
{
    (void)clk_id;
    (void)rate;
    (void)parent_rate;
    return 0;
}

unsigned long hal_clk_recalc_rate(uint32_t clk_id, unsigned long parent_rate)
{
    (void)parent_rate;
    return hal_clk_get_freq(clk_id);
}

long hal_clk_round_rate(uint32_t clk_id, unsigned long rate,
                        unsigned long parent_rate)
{
    (void)clk_id;
    (void)parent_rate;
    return (long)rate;
}

int hal_clk_set_freq(uint32_t clk_id, unsigned long freq)
{
    struct aic_clk_desc desc;
    u32 div;
    u32 value;

    /*
     * Most peripheral clocks keep the bootloader-programmed rate.  DE and
     * GE are configured here because their divider must match the engine
     * timing; both are fixed-parent clocks of PLL_INT1 (1.2 GHz).
     */
    if (clk_id != CLK_DE && clk_id != CLK_GE)
        return 0;

    if (freq == 0 || aic_clk_lookup(clk_id, &desc) < 0)
        return -EINVAL;

    div = (1200000000UL + freq / 2) / freq;
    if (div == 0)
        div = 1;
    if (div > 32)
        return -ERANGE;

    value = CMU_REG(desc.offset);
    value &= ~0x1FU;
    value |= (div - 1) & 0x1F;
    CMU_REG(desc.offset) = value;

    return 0;
}

const char *hal_clk_get_name(uint32_t clk_id)
{
    (void)clk_id;
    return "aic-clk";
}
