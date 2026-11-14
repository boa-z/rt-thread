/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip UART register layout.
 *
 * D21x UART v1.0 and D12p UART v1.4 use the same DesignWare-derived
 * 16550 register block; the offsets and the fields below are shared.
 * FIFO depth, clock source, DMA handshake availability and the full
 * RXCTL feature set still differ per SoC, so this header only describes
 * the registers, not a driver policy.
 *
 * Derived from the ArtInChip Luban-Lite HAL
 * (bsp/artinchip/hal/uart/aic_hal_uart.c, Apache-2.0).
 */

#ifndef __AIC_UART_REG_H__
#define __AIC_UART_REG_H__

/* Register offsets */
#define UART_RBR    0x00
#define UART_THR    0x00
#define UART_DLL    0x00
#define UART_DLH    0x04
#define UART_IER    0x04
#define UART_IIR    0x08
#define UART_FCR    0x08
#define UART_LCR    0x0C
#define UART_MCR    0x10
#define UART_LSR    0x14
#define UART_MSR    0x18
#define UART_USR    0x7C
#define UART_RFL    0x84
#define UART_HSK    0x88
#define UART_RXCTL  0xA0
#define UART_HALT   0xA4
#define UART_EXREG  0xB8

/* Line status register */
#define UART_LSR_DR     0x01
#define UART_LSR_OE     0x02
#define UART_LSR_PE     0x04
#define UART_LSR_FE     0x08
#define UART_LSR_BI     0x10
#define UART_LSR_THRE   0x20
#define UART_LSR_TEMT   0x40

/* Line control register */
#define UART_LCR_DLAB   0x80
#define UART_LCR_STOP   0x04
#define UART_LCR_PARITY 0x08
#define UART_LCR_EVEN   0x10
#define UART_LCR_8N1    0x03

/* FIFO control register */
#define UART_FCR_ENABLE     0x01
#define UART_FCR_RX_RESET   0x02
#define UART_FCR_TX_RESET   0x04
#define UART_FCR_DMA_MODE   (1U << 3)
/* 0: empty/1 byte, 3: 1/2 or full; the meaning differs for TX and RX. */
#define UART_FCR_TX_TRIG(x) ((x) << 4)
#define UART_FCR_RX_TRIG(x) ((x) << 6)

/* Modem control register */
#define UART_MCR_LOOPBACK  0x10
/* RTS control + auto flow control, from the vendor HAL. */
#define UART_MCR_AUTO_FLOW 0x22

/* UART status register */
#define UART_USR_BUSY 0x01
#define UART_USR_TFE  0x04
#define UART_USR_RFNE 0x08

/* ArtInChip extension registers */
#define UART_RXCTL_ENABLE 0x01
#define UART_HSK_DMA_MODE 0xE5
#define UART_HALT_TX_DRQ  0x40

#endif /* __AIC_UART_REG_H__ */
