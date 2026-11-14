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

/*
 * PLL_FRA0 is fixed by the ArtInChip bootloader for QSPI/SDMC.  The
 * demo128-nand configuration uses 600 MHz, the same value assumed by the
 * vendor sys_clk.c.  SDMC divides it with the 5-bit field in its CMU
 * register (value N means divide by N + 1).
 */
#define CLK_PLL_FRA0_FREQ   600000000UL
#define CLK_SDMC_DIV_MASK   0x1fU

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

    if (id >= CLK_SDMC0 && id <= CLK_SDMC2)
    {
        desc->offset = CLK_SDMC0_REG + (id - CLK_SDMC0) * 4;
        desc->bus_gate_bit = 12;
        desc->mod_gate_bit = 8;
        desc->freq = 100000000UL;
        return 0;
    }

    switch (id)
    {
    case CLK_DMA:
        /* AHB bus clock; the DMA block has no separate module gate. */
        desc->offset = CLK_DMA_REG;
        desc->bus_gate_bit = 12;
        desc->mod_gate_bit = -1;
        desc->freq = 0;
        return 0;
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
    case CLK_VE:
        desc->offset = CLK_VE_REG;
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
    case CLK_CODEC:
        /*
         * Codec clock: PLL_FRA1 divided by the 5-bit field in
         * CLK_CODEC_REG.  The audio HAL sets the parent to
         * samplerate_module_clock * 20 and the divider to 20, giving
         * 24.576 MHz (or 22.5792 MHz) at the module.
         */
        desc->offset = CLK_CODEC_REG;
        desc->bus_gate_bit = 12;
        desc->mod_gate_bit = 8;
        desc->freq = 24576000UL;
        return 0;
    case CLK_USBH0:
        /* AHB0 gate; the host controller has no separate module gate. */
        desc->offset = CLK_USBH0_REG;
        desc->bus_gate_bit = 12;
        desc->mod_gate_bit = -1;
        desc->freq = 100000000UL;
        return 0;
    case CLK_USBD:
        /*
         * AHB0 gate shared by the UDC.  The vendor clock tree derives
         * its rate from CLK_AHB0, which this BSP models as 240 MHz.
         */
        desc->offset = CLK_USBD_REG;
        desc->bus_gate_bit = 12;
        desc->mod_gate_bit = -1;
        desc->freq = 240000000UL;
        return 0;
    case CLK_USBH1:
        desc->offset = CLK_USBH1_REG;
        desc->bus_gate_bit = 12;
        desc->mod_gate_bit = -1;
        desc->freq = 100000000UL;
        return 0;
    case CLK_USB_PHY0:
        /* PHY serial clock from OSC24M; only the module gate exists. */
        desc->offset = CLK_USB_PHY0_REG;
        desc->bus_gate_bit = -1;
        desc->mod_gate_bit = 8;
        desc->freq = 24000000UL;
        return 0;
    case CLK_USB_PHY1:
        desc->offset = CLK_USB_PHY1_REG;
        desc->bus_gate_bit = -1;
        desc->mod_gate_bit = 8;
        desc->freq = 24000000UL;
        return 0;
    case CLK_SYSCFG:
        desc->offset = CLK_SYSCFG_REG;
        desc->bus_gate_bit = 12;
        desc->mod_gate_bit = -1;
        desc->freq = 24000000UL;
        return 0;
    case CLK_GMAC0:
        desc->offset = CLK_GMAC0_REG;
        desc->bus_gate_bit = 12;
        desc->mod_gate_bit = 8;
        desc->freq = 50000000UL;
        return 0;
    case CLK_GMAC1:
        desc->offset = CLK_GMAC1_REG;
        desc->bus_gate_bit = 12;
        desc->mod_gate_bit = 8;
        desc->freq = 50000000UL;
        return 0;
    case CLK_OUT2:
        /*
         * PE.10 clock output feeding the RTL8201F XTAL_IN.  The mux
         * field selects PLL_INT1 (index 0) and the 8-bit divider field
         * stores N - 1, giving 1.2 GHz / 48 = 25 MHz for N = 48.
         */
        desc->offset = CLK_OUT2_REG;
        desc->bus_gate_bit = -1;
        desc->mod_gate_bit = 16;
        desc->freq = 25000000UL;
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
    case CLK_TSEN:
        /* APB1 clock; only the bus gate exists. */
        desc->offset = CLK_TSEN_REG;
        desc->bus_gate_bit = 12;
        desc->mod_gate_bit = -1;
        desc->freq = 24000000UL;
        return 0;
    case CLK_SID:
        /* OSC24M clock for the eFuse controller. */
        desc->offset = CLK_SID_REG;
        desc->bus_gate_bit = 12;
        desc->mod_gate_bit = 8;
        desc->freq = 24000000UL;
        return 0;
    case CLK_CE:
        /*
         * Crypto Engine clock.  The driver only gates and resets the
         * block; the divider is left as programmed by the bootloader.
         */
        desc->offset = CLK_CE_REG;
        desc->bus_gate_bit = 12;
        desc->mod_gate_bit = 8;
        desc->freq = 0;
        return 0;
    case CLK_ADCIM:
        /* Analog common block shared by the temperature sensor. */
        desc->offset = CLK_ADCIM_REG;
        desc->bus_gate_bit = 12;
        desc->mod_gate_bit = 8;
        desc->freq = 48000000UL;
        return 0;
    case CLK_GPAI:
        /* General-purpose ADC; APB1 clock, bus gate only. */
        desc->offset = CLK_GPAI_REG;
        desc->bus_gate_bit = 12;
        desc->mod_gate_bit = -1;
        desc->freq = 24000000UL;
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
    u32 value;

    if (clk_id == CLK_PIX || clk_id == CLK_SCLK || clk_id == CLK_PLL_FRA2)
        return 1;

    if (aic_clk_lookup(clk_id, &desc) < 0)
        return 0;

    value = CMU_REG(desc.offset);
    if (desc.bus_gate_bit >= 0 && !(value & CMU_MOD_GATE(desc.bus_gate_bit)))
        return 0;
    if (desc.mod_gate_bit >= 0 && !(value & CMU_MOD_GATE(desc.mod_gate_bit)))
        return 0;

    return 1;
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

/*
 * PLL_FRA1 and the audio codec.
 *
 * The codec module needs an exact 24.576 MHz (48/32/24/16/12/8 kHz
 * families) or 22.5792 MHz (44.1/22.05/11.025 kHz) clock.  The vendor
 * HAL gets it by programming PLL_FRA1 to module_freq * 20 and dividing
 * it by 20 in CLK_CODEC_REG.  This BSP does not run the full CMU
 * framework, so the fractional-PLL sequence is reproduced here for the
 * one PLL that audio uses.  Nothing else in this BSP selects PLL_FRA1
 * (I2S0/I2S1 are not enabled), so reprogramming it is safe.
 *
 * Sequence and factor arithmetic follow the vendor
 * hal/cmu/aic_hal_pll_clk.c (Apache-2.0, ArtInChip).
 */
#define AIC_PLL_FRA1_MIN_VCO    768000000UL
#define AIC_PLL_FRA1_MAX_VCO    1560000000UL
#define AIC_PLL_PARENT_FREQ     24000000UL

static void aic_pll_fra1_set_rate(unsigned long rate)
{
    u32 gen, reg, factor_n, factor_m, factor_p, factor_m_en;
    u64 fra_in = 0;
    unsigned long vco_rate, int_div, rem;

    /* VCO must stay in 768..1560 MHz; pick the post divider first. */
    if (rate < AIC_PLL_FRA1_MIN_VCO)
        factor_m = DIV_ROUND_UP(AIC_PLL_FRA1_MIN_VCO, rate) - 1;
    else
        factor_m = 0;

    /* The vendor driver skips factor_m == 2 for stability. */
    if (factor_m == 2)
        factor_m = 3;
    if (factor_m > PLL_FACTORM_MASK)
        factor_m = PLL_FACTORM_MASK;
    factor_m_en = factor_m ? 1 : 0;

    vco_rate = (factor_m + 1) * rate;
    if (vco_rate > AIC_PLL_FRA1_MAX_VCO)
        vco_rate = AIC_PLL_FRA1_MAX_VCO;

    factor_p = (vco_rate % AIC_PLL_PARENT_FREQ) ? 1 : 0;
    factor_n = vco_rate * (factor_p + 1) / AIC_PLL_PARENT_FREQ - 1;

    /* Route the output to 24 MHz while the factors are changed. */
    gen = CMU_REG(PLL_FRA1_GEN_REG);
    CMU_REG(PLL_FRA1_GEN_REG) = gen & ~(1U << PLL_OUT_MUX);

    reg = CMU_REG(PLL_FRA1_GEN_REG);
    reg &= ~0xFFFFU;
    reg |= (factor_m_en << PLL_FACTORM_EN_BIT) |
           (factor_n << PLL_FACTORN_BIT) |
           (factor_m << PLL_FACTORM_BIT) |
           (factor_p << PLL_FACTORP_BIT);
    CMU_REG(PLL_FRA1_GEN_REG) = reg;

    /* Fill the residue with the fractional divider. */
    int_div = AIC_PLL_PARENT_FREQ * (factor_n + 1) /
              (factor_m + 1) / (factor_p + 1);
    rem = rate % int_div;
    if (rem)
    {
        fra_in = (u64)rem * (factor_p + 1) * (factor_m + 1) *
                 PLL_FRAC_DIV_MASK;
        do_div(fra_in, AIC_PLL_PARENT_FREQ);
    }
    CMU_REG(PLL_FRA1_CFG_REG) = ((rem ? 1U : 0U) << PLL_FRAC_EN_BIT) |
                                ((u32)fra_in & PLL_FRAC_DIV_MASK);

    /* Fractional mode: SDM on, fixed mode, no spread spectrum. */
    CMU_REG(PLL_FRA1_SDM_REG) = (1U << PLL_SDM_EN_BIT) |
                                (2U << PLL_SDM_MODE_BIT);

    reg = CMU_REG(PLL_FRA1_GEN_REG);
    reg |= (1U << PLL_OUT_SYS) | (1U << PLL_EN_BIT);
    CMU_REG(PLL_FRA1_GEN_REG) = reg;

    /*
     * Wait for the loop to lock, then switch the output mux back to the
     * PLL.  The bypass above left PLL_OUT_MUX on the 24 MHz parent; if it
     * is not cleared here the codec runs at 24 MHz/20 = 1.2 MHz instead
     * of 24.576 MHz, i.e. every audio rate is ~20x too slow.
     */
    udelay(200);
    reg = CMU_REG(PLL_FRA1_GEN_REG);
    reg |= (1U << PLL_OUT_MUX);
    CMU_REG(PLL_FRA1_GEN_REG) = reg;
}

static unsigned long aic_pll_fra1_get_rate(void)
{
    u32 gen = CMU_REG(PLL_FRA1_GEN_REG);
    u32 cfg = CMU_REG(PLL_FRA1_CFG_REG);
    u32 factor_n, factor_m, factor_p, fra_in;
    u64 rate;

    if (!((gen >> PLL_OUT_MUX) & 0x1))
        return AIC_PLL_PARENT_FREQ;

    factor_n = (gen >> PLL_FACTORN_BIT) & PLL_FACTORN_MASK;
    factor_m = (gen >> PLL_FACTORM_BIT) & PLL_FACTORM_MASK;
    factor_p = (gen >> PLL_FACTORP_BIT) & PLL_FACTORP_MASK;

    rate = (u64)AIC_PLL_PARENT_FREQ / (factor_p + 1) * (factor_n + 1) /
           (factor_m + 1);

    if (cfg & (1U << PLL_FRAC_EN_BIT))
    {
        fra_in = cfg & PLL_FRAC_DIV_MASK;
        rate += (u64)AIC_PLL_PARENT_FREQ / (factor_p + 1) * fra_in /
                (PLL_FRAC_DIV_MASK * (factor_m + 1));
    }

    return (unsigned long)rate;
}

unsigned long hal_clk_get_freq(uint32_t clk_id)
{
    struct aic_clk_desc desc;

    if (clk_id == CLK_PLL_FRA0)
        return CLK_PLL_FRA0_FREQ;
    if (clk_id == CLK_PLL_INT1)
        return 1200000000UL;
    if (clk_id == CLK_PLL_FRA1)
        return aic_pll_fra1_get_rate();
    if (clk_id == CLK_AHB0)
        return 240000000UL;
    if (clk_id == CLK_APB1)
        return 24000000UL;
    if (clk_id == CLK_OUT2)
        return 1200000000UL / ((CMU_REG(CLK_OUT2_REG) & 0xFFU) + 1);

    if (aic_clk_lookup(clk_id, &desc) < 0)
        return 24000000UL;

    /* SDMC and DE/GE/VE derive their rate from the programmed divider. */
    if ((clk_id >= CLK_SDMC0 && clk_id <= CLK_SDMC2) ||
        clk_id == CLK_DE || clk_id == CLK_GE || clk_id == CLK_VE || clk_id == CLK_GMAC0 ||
        clk_id == CLK_GMAC1)
    {
        u32 div = (CMU_REG(desc.offset) & CLK_SDMC_DIV_MASK) + 1;
        unsigned long parent = (clk_id >= CLK_SDMC0 &&
                                clk_id <= CLK_SDMC2) ?
                               CLK_PLL_FRA0_FREQ : 1200000000UL;

        return parent / div;
    }

    if (clk_id == CLK_CODEC)
    {
        u32 div = (CMU_REG(desc.offset) & CLK_SDMC_DIV_MASK) + 1;

        return aic_pll_fra1_get_rate() / div;
    }

    return desc.freq;
}

unsigned int hal_clk_get_parent(uint32_t clk_id)
{
    if (clk_id >= CLK_SDMC0 && clk_id <= CLK_SDMC2)
        return CLK_PLL_FRA0;

    if (clk_id == CLK_CODEC)
        return CLK_PLL_FRA1;

    /* TSEN samples on the APB1 clock; the vendor HAL derives the
     * period-mode interval from this parent rate. */
    if (clk_id == CLK_TSEN)
        return CLK_APB1;

    /* GPAI is clocked from APB1; the HAL converts its sample period
     * from this rate. */
    if (clk_id == CLK_GPAI)
        return CLK_APB1;

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
    u32 div, value;

    /*
     * Only the codec has a settable divider in this BSP.  Its parent is
     * programmed separately by hal_clk_set_freq(CLK_PLL_FRA1, ...).
     */
    if (clk_id != CLK_CODEC || rate == 0 || parent_rate == 0)
        return 0;

    div = (parent_rate + rate / 2) / rate;
    if (div == 0)
        div = 1;
    if (div > 32)
        return -ERANGE;

    value = CMU_REG(CLK_CODEC_REG);
    value &= ~CLK_SDMC_DIV_MASK;
    value |= (div - 1) & CLK_SDMC_DIV_MASK;
    CMU_REG(CLK_CODEC_REG) = value;

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
    unsigned long parent;
    u32 div;
    u32 value;

    /*
     * Most peripheral clocks keep the bootloader-programmed rate.  DE/GE
     * and SDMC are configured here because their divider must match the
     * controller timing: DE/GE use PLL_INT1 (1.2 GHz), SDMC uses
     * PLL_FRA0 (600 MHz).  The audio path programs PLL_FRA1 and the
     * codec divider separately through hal_clk_set_rate().
     */
    if (clk_id == CLK_CODEC)
        return 0;
    if (clk_id == CLK_PLL_FRA1)
    {
        if (freq == 0)
            return -EINVAL;
        aic_pll_fra1_set_rate(freq);
        return 0;
    }
    if (clk_id == CLK_DE || clk_id == CLK_GE || clk_id == CLK_VE ||
        clk_id == CLK_GMAC0 || clk_id == CLK_GMAC1)
        parent = 1200000000UL;
    else if (clk_id >= CLK_SDMC0 && clk_id <= CLK_SDMC2)
        parent = CLK_PLL_FRA0_FREQ;
    else if (clk_id == CLK_OUT2)
    {
        if (freq == 0)
            return -EINVAL;

        div = (1200000000UL + freq / 2) / freq;
        if (div == 0 || div > 256)
            return -ERANGE;

        value = CMU_REG(CLK_OUT2_REG);
        /* Parent 0 = PLL_INT1, divider field [7:0] = N - 1. */
        value &= ~((0x7U << 12) | 0xFFU);
        value |= (div - 1) & 0xFFU;
        CMU_REG(CLK_OUT2_REG) = value;
        return 0;
    }
    else
        return 0;

    if (freq == 0 || aic_clk_lookup(clk_id, &desc) < 0)
        return -EINVAL;

    div = (parent + freq / 2) / freq;
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
