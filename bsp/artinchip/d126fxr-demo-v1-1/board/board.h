/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D126 (D12p) board definitions.
 */

#ifndef __BOARD_H__
#define __BOARD_H__

#include <rtthread.h>
#include <interrupt.h>

#ifndef MAX_HANDLERS
#define MAX_HANDLERS            128
#endif

#ifndef INTC_TYPE
#define INTC_TYPE               INTC_TYPE_CLIC
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Confirmed from bsp/artinchip/sys/d12p/include/aic_soc.h. */
#define AIC_SRAM_BASE           0x30040000UL
#define AIC_SRAM_SIZE           0x00010000UL
#define AIC_PSRAM_BASE          0x40000000UL
#ifndef AIC_PSRAM_SIZE
#define AIC_PSRAM_SIZE          0x00800000UL
#endif
#define AIC_CPU_BASE            0x20000000UL
#define AIC_CLIC_BASE           (AIC_CPU_BASE + 0x00800000UL)
#define AIC_CORET_BASE          (AIC_CPU_BASE + 0x00004000UL)
#define AIC_CMU_BASE            0x18020000UL
#define AIC_GPIO_BASE           0x10700000UL
#define AIC_UART0_BASE          0x18710000UL
/* Luban-Lite bsp/artinchip/sys/d12p/include/aic_soc.h: UART0_IRQn. */
#define AIC_UART0_IRQ           78

#define AIC_PLL_INT0_GEN_REG    0x0000UL
#define AIC_PLL_INT1_GEN_REG    0x0004UL
#define AIC_GPIO_CLK_REG        0x083CUL
#define AIC_UART0_CLK_REG       0x0840UL
#define AIC_CPU_CLK_REG         0x0180UL

#define AIC_CLIC_IRQ_NUM        128

/* UART0 pins from the vendor PBP configuration: PA0/PA1. */
#define AIC_PA0_PINMUX_REG      0x10700080UL
#define AIC_PA1_PINMUX_REG      0x10700084UL
#define AIC_UART0_PINMUX_VAL    0x00000325UL

#ifndef BSP_UART0_BAUD
#define BSP_UART0_BAUD          115200
#endif

void aic_board_clock_init(void);
unsigned long aic_uart_clock(void);
unsigned long aic_cpu_clock(void);

void rt_hw_uart_init(void);
void rt_hw_tick_init(void);
void rt_hw_interrupt_init(void);

void aic_irq_dispatch(rt_uint32_t irq);
void aic_exception_handler(rt_ubase_t mcause, rt_ubase_t mepc,
                           rt_ubase_t mtval, rt_ubase_t *regs);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_H__ */
