/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D21x CMU gate control.
 *
 * The CPU, AXI/AHB/APB and UART0 clock tree is configured by the
 * ArtInChip bootloader before the RT-Thread image is entered.  This
 * file only enables the peripheral gates used by the BSP and releases
 * their resets.  Register offsets are from the vendor aic_clk_id.h.
 */

#include <rthw.h>
#include <rtthread.h>
#include <stdint.h>

#include "board.h"

#define CMU_REG(off) (*(volatile uint32_t *)(AIC_CMU_BASE + (off)))

#define CLK_BUS_GATE      (1U << 12)
#define CLK_MOD_GATE(bit) (1U << (bit))
#define CLK_RSTN          (1U << 13)

#define CLK_GPIO_REG  0x083C
#define CLK_UART0_REG 0x0840
#define CLK_GTC_REG   0x090C

#define CLK_UART_REG(n) (CLK_UART0_REG + (n) * 4)

static void aic_clk_enable(uint32_t offset, int mod_gate_bit)
{
    uint32_t value;

    value = CMU_REG(offset);
    value |= CLK_BUS_GATE;
    if (mod_gate_bit >= 0)
    {
        value |= CLK_MOD_GATE(mod_gate_bit);
    }
    CMU_REG(offset) = value;

    rt_hw_us_delay(30);

    value = CMU_REG(offset);
    value |= CLK_RSTN;
    CMU_REG(offset) = value;

    rt_hw_us_delay(30);
}

void aic_board_sysclk_init(void)
{
    /* UART: bus gate bit 12, module gate bit 8, reset bit 13. */
#if defined(BSP_USING_UART0)
    aic_clk_enable(CLK_UART_REG(0), 8);
#endif
#if defined(BSP_USING_UART1)
    aic_clk_enable(CLK_UART_REG(1), 8);
#endif
#if defined(BSP_USING_UART2)
    aic_clk_enable(CLK_UART_REG(2), 8);
#endif
#if defined(BSP_USING_UART3)
    aic_clk_enable(CLK_UART_REG(3), 8);
#endif
#if defined(BSP_USING_UART4)
    aic_clk_enable(CLK_UART_REG(4), 8);
#endif
#if defined(BSP_USING_UART5)
    aic_clk_enable(CLK_UART_REG(5), 8);
#endif
#if defined(BSP_USING_UART6)
    aic_clk_enable(CLK_UART_REG(6), 8);
#endif
#if defined(BSP_USING_UART7)
    aic_clk_enable(CLK_UART_REG(7), 8);
#endif

    /* GPIO and GTC: bus gate only. */
    aic_clk_enable(CLK_GPIO_REG, -1);
    aic_clk_enable(CLK_GTC_REG, -1);
}
