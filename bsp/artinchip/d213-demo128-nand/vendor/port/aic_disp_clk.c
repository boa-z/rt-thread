/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include "aic_compat.h"
#include "aic_hal_clk.h"
#include "aic_disp_clk.h"

#define PLL_MIN_RATE            360000000UL
#define PLL_MAX_RATE            1584000000UL
#define PLL_REF_RATE            24000000UL

#define PLL_GEN_REG             PLL_FRA2_GEN_REG
#define PLL_FRA_REG             PLL_FRA2_CFG_REG
#define PLL_SDM_REG             PLL_FRA2_SDM_REG

#define PLL_REG(off)            (*(volatile u32 *)(CMU_BASE + (off)))

static int aic_disp_pll_set(uint32_t rate)
{
    uint32_t parent = PLL_REF_RATE;
    uint32_t m = 0, n, p;
    uint32_t vco = rate;
    uint32_t fra_in = 0;
    uint32_t fra_en = 0;
    uint32_t gen;
    uint32_t base;
    u64 val;

    if (rate < PLL_MIN_RATE || rate > PLL_MAX_RATE)
        return -ERANGE;

    p = (rate % parent) ? 1 : 0;
    /* Keep the VCO inside the documented range for PLL_FRA2. */
    if (vco < PLL_MIN_RATE)
        return -ERANGE;

    base = parent / (p + 1);
    n = (rate / base) - 1;
    if (n > 0xff)
        return -ERANGE;

    val = rate - (u64)(n + 1) * base;
    fra_en = val ? 1 : 0;
    if (fra_en)
    {
        u64 fraction = val * (p + 1) * PLL_FRAC_DIV_MASK;
        do_div(fraction, parent);
        fra_in = (uint32_t)fraction & PLL_FRAC_DIV_MASK;
    }

    /* Select the 24 MHz reference while changing the PLL. */
    gen = PLL_REG(PLL_GEN_REG);
    gen &= ~BIT(PLL_OUT_MUX);
    PLL_REG(PLL_GEN_REG) = gen;

    gen &= ~(0xFFFFU | BIT(PLL_FACTORM_EN_BIT) | (0x1FU << 24));
    gen |= (m << PLL_FACTORM_BIT) |
           (n << PLL_FACTORN_BIT) |
           (p << PLL_FACTORP_BIT);
    PLL_REG(PLL_GEN_REG) = gen;

    PLL_REG(PLL_FRA_REG) = (fra_en << PLL_FRAC_EN_BIT) |
                           (fra_in & PLL_FRAC_DIV_MASK);
    PLL_REG(PLL_SDM_REG) = (1U << PLL_SDM_EN_BIT) |
                           (2U << PLL_SDM_MODE_BIT);

    gen |= BIT(PLL_OUT_SYS) | BIT(PLL_EN_BIT);
    PLL_REG(PLL_GEN_REG) = gen;

    /*
     * The D21x vendor PLL path waits a fixed 200 us and does not poll
     * PLL_LOCK_BIT.  On this SoC that status bit is not a reliable
     * "new rate locked" indicator while the output mux is bypassed.
     */
    udelay(200);

    gen |= BIT(PLL_OUT_MUX);
    PLL_REG(PLL_GEN_REG) = gen;

    return 0;
}

static int aic_log2_exact(uint32_t value, uint32_t *log2_out)
{
    uint32_t shift = 0;

    if (value == 0)
        return -EINVAL;

    while ((value & 1U) == 0 && value > 1)
    {
        value >>= 1;
        shift++;
    }

    if (value != 1)
        return -EINVAL;

    *log2_out = shift;
    return 0;
}

int aic_disp_clk_set(uint32_t pixclk, int link_mode, uint32_t *sclk_out)
{
    uint32_t sclk;
    uint32_t pll;
    uint32_t divn = 0;
    uint32_t divm;
    uint32_t divl = 0;
    uint32_t pixsel = 0;
    uint32_t div;
    uint32_t value;
    int ret;

    if (pixclk == 0)
        return -EINVAL;

    if (link_mode == 0 || link_mode == 1)
        sclk = pixclk * 7U;
    else if (link_mode == 3)
        sclk = pixclk * 7U / 2U;
    else
        return -EINVAL;

    pll = sclk;
    while (pll < PLL_MIN_RATE)
        pll <<= 1;

    ret = aic_disp_pll_set(pll);
    if (ret)
        return ret;

    if (pll % sclk)
        return -EINVAL;

    div = pll / sclk;
    ret = aic_log2_exact(div, &divn);
    if (ret || divn > 7)
        return -EINVAL;

    div = sclk / pixclk;
    if (div == 0 || div > 32 || (sclk % pixclk) != 0)
    {
        /* The D21x PIX divider also supports fixed 2/7 and 2/9 modes. */
        if (sclk * 2U / 7U == pixclk)
        {
            pixsel = 1;
            divm = 0;
        }
        else if (sclk * 2U / 9U == pixclk)
        {
            pixsel = 2;
            divm = 0;
        }
        else
        {
            return -EINVAL;
        }
    }
    else
    {
        divm = div - 1;
    }

    value = PLL_REG(CLK_DISP_REG);
    value &= ~GENMASK(13, 0);
    value |= (divn & 0x7) | ((divm & 0x1f) << 4) |
             ((divl & 0x3) << 10) | ((pixsel & 0x3) << 12);
    PLL_REG(CLK_DISP_REG) = value;

    if (sclk_out)
        *sclk_out = sclk;

    return 0;
}
