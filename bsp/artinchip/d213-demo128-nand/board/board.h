/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D21x / D213 board support.
 *
 * The register definitions in this file are derived from the ArtInChip
 * Luban-Lite D21x SoC headers, which are licensed under Apache-2.0:
 *   bsp/artinchip/sys/d21x/include/aic_soc.h
 *   bsp/artinchip/sys/d21x/include/aic_clk_id.h
 *   bsp/artinchip/hal/gpio/aic_hal_gpio.c
 */

#ifndef __BOARD_H__
#define __BOARD_H__

#include <rtthread.h>
#include <interrupt.h>

#ifndef MAX_HANDLERS
#define MAX_HANDLERS              128
#endif

#ifndef INTC_TYPE
#define INTC_TYPE                 INTC_TYPE_RISCV
#endif

#define AIC_DRAM_BASE             0x40000000UL

#define AIC_CMU_BASE              0x18020000UL
#define AIC_GPIO_BASE             0x18700000UL
#define AIC_QSPI0_BASE            0x10400000UL

#define AIC_UART_BASE(n)          (0x18710000UL + (n) * 0x1000UL)
#define AIC_UART0_BASE            AIC_UART_BASE(0)

#define AIC_PLIC_BASE             0x20000000UL
#define AIC_CLINT_BASE            0x24000000UL

#define AIC_UART_IRQ(n)           (76 + (n))
#define AIC_UART0_IRQ             AIC_UART_IRQ(0)
#define AIC_QSPI0_IRQ             44

#define AIC_CLINT_FREQ            BSP_CLINT_FREQ
#define AIC_UART_CLK_FREQ         BSP_UART_CLK_FREQ
#define AIC_UART0_CLK_FREQ        AIC_UART_CLK_FREQ

void aic_board_sysclk_init(void);
void aic_board_pinmux_init(void);
void aic_board_uart0_init(void);
void aic_plic_quirk_init(void);

#endif /* __BOARD_H__ */
