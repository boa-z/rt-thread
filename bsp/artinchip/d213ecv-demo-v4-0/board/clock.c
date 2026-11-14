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

/* Function-clock divider field, bits [4:0] in every UART CMU register. */
#define CLK_UART_DIV_MASK 0x1FU

#define CLK_GPIO_REG  0x083C
#define CLK_UART0_REG 0x0840
#define CLK_QSPI0_REG 0x0460
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

/*
 * The bootloader configures UART0's function clock for the console but
 * leaves UART1..7 with a zero divider, which stops their module clock:
 * register reads return 0 and the transmitter never drains the FIFO.
 * Copy UART0's divider before enabling the gates.  All instances share
 * the same parent and target rate (BSP_UART_CLK_FREQ).
 */
static void aic_uart_clk_enable(int id)
{
    uint32_t offset = CLK_UART_REG(id);

    if (id != 0)
    {
        uint32_t value = CMU_REG(offset) & ~CLK_UART_DIV_MASK;

        value |= CMU_REG(CLK_UART0_REG) & CLK_UART_DIV_MASK;
        CMU_REG(offset) = value;
    }

    aic_clk_enable(offset, 8);
}

void aic_board_sysclk_init(void)
{
    /* UART: bus gate bit 12, module gate bit 8, reset bit 13. */
#if defined(BSP_USING_UART0)
    aic_uart_clk_enable(0);
#endif
#if defined(BSP_USING_UART1)
    aic_uart_clk_enable(1);
#endif
#if defined(BSP_USING_UART2)
    aic_uart_clk_enable(2);
#endif
#if defined(BSP_USING_UART3)
    aic_uart_clk_enable(3);
#endif
#if defined(BSP_USING_UART4)
    aic_uart_clk_enable(4);
#endif
#if defined(BSP_USING_UART5)
    aic_uart_clk_enable(5);
#endif
#if defined(BSP_USING_UART6)
    aic_uart_clk_enable(6);
#endif
#if defined(BSP_USING_UART7)
    aic_uart_clk_enable(7);
#endif

    /* GPIO and GTC: bus gate only. */
    aic_clk_enable(CLK_GPIO_REG, -1);
    aic_clk_enable(CLK_GTC_REG, -1);

#if defined(BSP_USING_QSPI0)
    /* QSPI0: bus gate bit 12, module gate bit 8, reset bit 13. Parent PLL_FRA0. */
    aic_clk_enable(CLK_QSPI0_REG, 8);
#endif
}
