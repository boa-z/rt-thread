/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D213 UART0 pinmux.
 *
 * PA.0 = UART0_TX, PA.1 = UART0_RX, function 5.
 * Register layout is from the vendor aic_hal_gpio.c.
 */

#include <rtthread.h>
#include <stdint.h>

#include "board.h"

#define GPIO_GROUP_FACTOR 0x100
#define GPIO_PIN_FACTOR   0x4
#define GPIO_PIN_CFG_REG  0x80

#define PIN_FUNC(v)  ((v) & 0xf)
#define PIN_DRIVE(v) (((v) & 0x7) << 4)
#define PIN_PULL(v)  (((v) & 0x3) << 8)

#define PIN_PULL_DIS 0
#define PIN_PULL_UP  3

static void aic_pin_cfg(uint32_t group, uint32_t pin, uint32_t value)
{
    volatile uint32_t *reg;

    reg = (volatile uint32_t *)(AIC_GPIO_BASE + group * GPIO_GROUP_FACTOR +
                                pin * GPIO_PIN_FACTOR + GPIO_PIN_CFG_REG);
    *reg = value;
}

void aic_board_pinmux_init(void)
{
#if defined(BSP_USING_UART0)
    /* UART0 TX: PA.0, function 5, no pull, drive strength 3. */
    aic_pin_cfg(0, 0, PIN_FUNC(5) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));

    /* UART0 RX: PA.1, function 5, pull-up, drive strength 3. */
    aic_pin_cfg(0, 1, PIN_FUNC(5) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_UP));
#endif

#if defined(BSP_USING_UART1)
    /* UART1 TX/RX: PA.4 / PA.5, function 5 (RS-232 transceiver). */
    aic_pin_cfg(0, 4, PIN_FUNC(5) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(0, 5, PIN_FUNC(5) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_UP));
#endif

#if defined(BSP_USING_UART2)
    /* UART2 TX/RX: PA.8 / PA.9, function 5.
     * These pins are shared with I2C2; do not enable both at once. */
    aic_pin_cfg(0, 8, PIN_FUNC(5) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(0, 9, PIN_FUNC(5) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_UP));
#endif

#if defined(BSP_USING_UART4)
    /* UART4 TX/RX: PB.6 / PB.7, function 5.
     * These pins are shared with SDMC0 and QSPI0. */
    aic_pin_cfg(1, 6, PIN_FUNC(5) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(1, 7, PIN_FUNC(5) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
#endif

#if defined(BSP_USING_I2C3)
    /* I2C3 SCK/SDA: PA.10 / PA.11, function 4 (touch controller). */
    aic_pin_cfg(0, 10, PIN_FUNC(4) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(0, 11, PIN_FUNC(4) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
#endif

#if defined(BSP_USING_CAN0)
    /* CAN0 TX/RX: PE.16 / PE.17, function 4. */
    aic_pin_cfg(4, 16, PIN_FUNC(4) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(4, 17, PIN_FUNC(4) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
#endif

#if defined(BSP_USING_CAN1)
    /* CAN1 TX/RX: PE.18 / PE.19, function 4. */
    aic_pin_cfg(4, 18, PIN_FUNC(4) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(4, 19, PIN_FUNC(4) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
#endif

#if defined(BSP_USING_PWM)
    /*
     * PWM0..PWM2: PC.0..PC.5, function 7.  PC.6/PC.7 are left alone:
     * PC.7 is the panel enable GPIO used by the display driver, so PWM3
     * must not claim it on this board.
     */
    aic_pin_cfg(2, 0, PIN_FUNC(7) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(2, 1, PIN_FUNC(7) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(2, 2, PIN_FUNC(7) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(2, 3, PIN_FUNC(7) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(2, 4, PIN_FUNC(7) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(2, 5, PIN_FUNC(7) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
#endif

#if defined(BSP_USING_DISPLAY)
#if BSP_DISPLAY_LVDS_LINK == 1
    /* LVDS link 1: PD.8..PD.17, function 3. */
    aic_pin_cfg(3, 8,  PIN_FUNC(3) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 9,  PIN_FUNC(3) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 10, PIN_FUNC(3) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 11, PIN_FUNC(3) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 12, PIN_FUNC(3) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 13, PIN_FUNC(3) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 14, PIN_FUNC(3) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 15, PIN_FUNC(3) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 16, PIN_FUNC(3) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 17, PIN_FUNC(3) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
#else
    /* LVDS link 0: PD.18..PD.27, function 3. */
    aic_pin_cfg(3, 18, PIN_FUNC(3) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 19, PIN_FUNC(3) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 20, PIN_FUNC(3) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 21, PIN_FUNC(3) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 22, PIN_FUNC(3) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 23, PIN_FUNC(3) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 24, PIN_FUNC(3) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 25, PIN_FUNC(3) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 26, PIN_FUNC(3) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 27, PIN_FUNC(3) | PIN_DRIVE(3) | PIN_PULL(PIN_PULL_DIS));
#endif
#endif

    /*
     * UART3/5/6/7 are not routed to a connector on the demo128 board and
     * the vendor board file does not define pinmux for them.  If a product
     * uses one of these instances, add the board-specific pinmux here.
     */
}
