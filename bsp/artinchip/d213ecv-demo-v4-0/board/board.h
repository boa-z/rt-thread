/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D21x / D213 board support.
 *
 * Hardware addresses live in the D21x aic_soc.h; this header exposes
 * board initialization entry points and board-selected input clocks.
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

#include "aic_soc.h"

#define AIC_CLINT_FREQ            BSP_CLINT_FREQ
#define AIC_UART_CLK_FREQ         BSP_UART_CLK_FREQ
#define AIC_UART0_CLK_FREQ        AIC_UART_CLK_FREQ

void aic_board_sysclk_init(void);
void aic_board_pinmux_init(void);
void aic_board_uart0_init(void);
void aic_plic_quirk_init(void);

#endif /* __BOARD_H__ */
