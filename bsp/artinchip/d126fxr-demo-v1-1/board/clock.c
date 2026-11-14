/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal D12p CMU support.
 *
 * The PBP has already configured the PLLs and pinmux.  This file only
 * derives the CPU and UART0 rates from the existing PLL/divider state
 * and keeps the UART0 module clock at the 48 MHz the bootloader
 * selected.  It deliberately does not reprogram the PLLs.
 */

#include <rtthread.h>
#include <stdint.h>

#include "board.h"

#define PLL_EN_BIT          16
#define PLL_OUT_MUX_BIT     20
#define PLL_FACTORN_BIT     8
#define PLL_FACTORN_MASK    0xff
#define PLL_FACTORM_BIT     4
#define PLL_FACTORM_MASK    0x3
#define PLL_FACTORP_BIT     0
#define PLL_FACTORP_MASK    0x1

#define XO_FREQ             24000000UL
/*
 * The bootloader console runs UART0 from a 48 MHz module clock
 * (PLL_INT1 / 25).  Use the same target so the divider we program is
 * identical to the one the bootloader already selected.
 */
#define UART_TARGET_FREQ    48000000UL

static unsigned long g_uart_clock;
static unsigned long g_cpu_clock;

static inline uint32_t aic_readl(uint32_t addr)
{
    return *(volatile uint32_t *)(uintptr_t)addr;
}

static inline void aic_writel(uint32_t value, uint32_t addr)
{
    *(volatile uint32_t *)(uintptr_t)addr = value;
}

static unsigned long aic_pll_rate(uint32_t gen_reg)
{
    uint32_t gen;
    uint32_t n, m, p;
    unsigned long rate;

    gen = aic_readl(AIC_CMU_BASE + gen_reg);

    /* PLL bypassed to the 24 MHz oscillator, or PLL not enabled. */
    if (!(gen & (1UL << PLL_OUT_MUX_BIT)) ||
        !(gen & (1UL << PLL_EN_BIT)))
    {
        return XO_FREQ;
    }

    n = (gen >> PLL_FACTORN_BIT) & PLL_FACTORN_MASK;
    m = (gen >> PLL_FACTORM_BIT) & PLL_FACTORM_MASK;
    p = (gen >> PLL_FACTORP_BIT) & PLL_FACTORP_MASK;

    if (n < 8 || n > 255)
    {
        return XO_FREQ;
    }

    rate = XO_FREQ / (p + 1);
    rate = rate * (n + 1);
    rate = rate / (m + 1);

    return rate;
}

unsigned long aic_cpu_clock(void)
{
    if (g_cpu_clock == 0)
    {
        g_cpu_clock = aic_pll_rate(AIC_PLL_INT0_GEN_REG);
    }
    return g_cpu_clock;
}

unsigned long aic_uart_clock(void)
{
    return g_uart_clock;
}

void aic_board_clock_init(void)
{
    unsigned long pll1;
    unsigned long uart_clk;
    uint32_t div;
    uint32_t reg;

    pll1 = aic_pll_rate(AIC_PLL_INT1_GEN_REG);
    reg = aic_readl(AIC_CMU_BASE + AIC_UART0_CLK_REG);

    if (pll1 < (2 * UART_TARGET_FREQ))
    {
        /* Unexpected PLL state: keep the bootloader divider and rate. */
        div = reg & 0x3f;
        uart_clk = pll1 / (div + 1);
        if (uart_clk < 1000000UL)
        {
            /*
             * The PLL readback is not usable.  Do not touch the divider;
             * the bootloader console was clocked from 48 MHz.
             */
            uart_clk = UART_TARGET_FREQ;
        }
    }
    else
    {
        div = (uint32_t)((pll1 + UART_TARGET_FREQ / 2) / UART_TARGET_FREQ);
        if (div == 0)
        {
            div = 1;
        }
        div -= 1;
        if (div > 0x3f)
        {
            div = 0x3f;
        }
        uart_clk = pll1 / (div + 1);
    }

    reg &= ~0x3fUL;
    reg |= div;
    /* pre-divider gate, module gate, APB gate and reset deassert. */
    reg |= (1UL << 17) | (1UL << 16) | (1UL << 20) | (1UL << 24);
    aic_writel(reg, AIC_CMU_BASE + AIC_UART0_CLK_REG);

    g_uart_clock = uart_clk;
    g_cpu_clock = aic_pll_rate(AIC_PLL_INT0_GEN_REG);
}
