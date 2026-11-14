/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 * D21x addresses and raw interrupt IDs from Luban-Lite sys/d21x.
 */
#ifndef __AIC_SOC_H__
#define __AIC_SOC_H__

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


#endif /* __AIC_SOC_H__ */
