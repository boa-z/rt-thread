/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip GPIO register layout.
 *
 * D21x GPIO v1.0 and D12p GPIO v1.2 use the same group/pin addressing
 * and the same GEN/PIN_CFG registers.  The available groups, the group
 * interrupt lines and the debounce field are SoC-specific and stay in
 * the SoC layer or the board driver.
 *
 * Derived from the ArtInChip Luban-Lite HAL
 * (bsp/artinchip/hal/gpio/aic_hal_gpio.c, Apache-2.0).
 */

#ifndef __AIC_GPIO_REG_H__
#define __AIC_GPIO_REG_H__

/* Group and pin addressing */
#define AIC_GPIO_GROUP_STRIDE   0x100
#define AIC_GPIO_PIN_STRIDE     0x4

/* Per-group registers */
#define AIC_GPIO_IN_STAT        0x00
#define AIC_GPIO_OUT_CFG        0x04
#define AIC_GPIO_IRQ_EN         0x08
#define AIC_GPIO_IRQ_STAT       0x0C
#define AIC_GPIO_OUT_CLR        0x10
#define AIC_GPIO_OUT_SET        0x14
#define AIC_GPIO_OUT_TOG        0x18

/* Per-pin configuration register */
#define AIC_GPIO_PIN_CFG        0x80

#define AIC_GPIO_PIN_FUNC_MASK       0x0FU
#define AIC_GPIO_PIN_DRIVE_SHIFT     4
#define AIC_GPIO_PIN_DRIVE_MASK      (0x7U << AIC_GPIO_PIN_DRIVE_SHIFT)
#define AIC_GPIO_PIN_PULL_SHIFT      8
#define AIC_GPIO_PIN_PULL_MASK       (0x3U << AIC_GPIO_PIN_PULL_SHIFT)
#define AIC_GPIO_PIN_IRQ_MODE_SHIFT  12
#define AIC_GPIO_PIN_IRQ_MODE_MASK   (0x7U << AIC_GPIO_PIN_IRQ_MODE_SHIFT)
#define AIC_GPIO_PIN_DIR_SHIFT       16
#define AIC_GPIO_PIN_DIR_MASK        (0x3U << AIC_GPIO_PIN_DIR_SHIFT)

#define AIC_GPIO_PIN_FUNC(v)     ((v) & AIC_GPIO_PIN_FUNC_MASK)
#define AIC_GPIO_PIN_DRIVE(v)    (((v) & 0x7U) << AIC_GPIO_PIN_DRIVE_SHIFT)
#define AIC_GPIO_PIN_PULL(v)     (((v) & 0x3U) << AIC_GPIO_PIN_PULL_SHIFT)
#define AIC_GPIO_PIN_IRQ_MODE(v) (((v) & 0x7U) << AIC_GPIO_PIN_IRQ_MODE_SHIFT)
#define AIC_GPIO_PIN_DIR(v)      (((v) & 0x3U) << AIC_GPIO_PIN_DIR_SHIFT)

#endif /* __AIC_GPIO_REG_H__ */
