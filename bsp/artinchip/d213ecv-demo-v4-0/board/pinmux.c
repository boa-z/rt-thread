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
#include "aic_gpio_reg.h"

#define PIN_PULL_DIS 0
#define PIN_PULL_UP  3

static void aic_pin_cfg(uint32_t group, uint32_t pin, uint32_t value)
{
    volatile uint32_t *reg;

    reg = (volatile uint32_t *)(AIC_GPIO_BASE + group * AIC_GPIO_GROUP_STRIDE +
                                pin * AIC_GPIO_PIN_STRIDE + AIC_GPIO_PIN_CFG);
    *reg = value;
}

void aic_board_pinmux_init(void)
{
#if defined(BSP_USING_UART0)
    /* UART0 TX: PA.0, function 5, no pull, drive strength 3. */
    aic_pin_cfg(0, 0, AIC_GPIO_PIN_FUNC(5) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));

    /* UART0 RX: PA.1, function 5, pull-up, drive strength 3. */
    aic_pin_cfg(0, 1, AIC_GPIO_PIN_FUNC(5) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_UP));
#endif

#if defined(BSP_USING_UART1)
    /* UART1 TX/RX: PA.4 / PA.5, function 5 (RS-232 transceiver). */
    aic_pin_cfg(0, 4, AIC_GPIO_PIN_FUNC(5) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(0, 5, AIC_GPIO_PIN_FUNC(5) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_UP));
#endif

#if defined(BSP_USING_UART2)
    /* UART2 TX/RX: PA.8 / PA.9, function 5.
     * These pins are shared with I2C2; do not enable both at once. */
    aic_pin_cfg(0, 8, AIC_GPIO_PIN_FUNC(5) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(0, 9, AIC_GPIO_PIN_FUNC(5) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_UP));
#endif

#if defined(BSP_USING_UART4)
    /* UART4 TX/RX: PB.6 / PB.7, function 5.
     * These pins are shared with SDMC0 and QSPI0. */
    aic_pin_cfg(1, 6, AIC_GPIO_PIN_FUNC(5) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(1, 7, AIC_GPIO_PIN_FUNC(5) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
#endif

#if defined(BSP_USING_QSPI0)
    /*
     * QSPI0: PB.0..PB.5, function 3, pull-up, drive 3.
     * PB.2 is CS.  PB.0..PB.5 are NOT shared with UART4 (PB.6/PB.7),
     * but PB.0..PB.11 are shared with SDMC0 (function 2); do not enable
     * SDMC0 together with QSPI0 on demo128.
     * Source: Luban-Lite target/d21x/demo128-nand/pinmux.c (Apache-2.0).
     */
    aic_pin_cfg(1, 0, AIC_GPIO_PIN_FUNC(3) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_UP));
    aic_pin_cfg(1, 1, AIC_GPIO_PIN_FUNC(3) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_UP));
    aic_pin_cfg(1, 2, AIC_GPIO_PIN_FUNC(3) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_UP));
    aic_pin_cfg(1, 3, AIC_GPIO_PIN_FUNC(3) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_UP));
    aic_pin_cfg(1, 4, AIC_GPIO_PIN_FUNC(3) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_UP));
    aic_pin_cfg(1, 5, AIC_GPIO_PIN_FUNC(3) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_UP));
#endif

#if defined(BSP_USING_I2C3)
    /* I2C3 SCK/SDA: PA.10 / PA.11, function 4 (touch controller). */
    aic_pin_cfg(0, 10, AIC_GPIO_PIN_FUNC(4) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(0, 11, AIC_GPIO_PIN_FUNC(4) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
#endif

#if defined(BSP_USING_GPAI6)
    /*
     * KEYADC: PA.6, function 2, no internal pull.  The board provides a
     * 10 k pull-up (R157) to VCC3V3 and the K4-K7 ladder to GND.
     * Source: Luban-Lite target/d21x/demo128-nand/pinmux.c (Apache-2.0)
     * and the D213ECV-DEMO-V4-0 schematic.
     */
    aic_pin_cfg(0, 6, AIC_GPIO_PIN_FUNC(2) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
#endif

#if defined(BSP_USING_CAN0)
    /* CAN0 TX/RX: PE.16 / PE.17, function 4. */
    aic_pin_cfg(4, 16, AIC_GPIO_PIN_FUNC(4) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(4, 17, AIC_GPIO_PIN_FUNC(4) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
#endif

#if defined(BSP_USING_CAN1)
    /* CAN1 TX/RX: PE.18 / PE.19, function 4. */
    aic_pin_cfg(4, 18, AIC_GPIO_PIN_FUNC(4) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(4, 19, AIC_GPIO_PIN_FUNC(4) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
#endif

#if defined(BSP_USING_SDMC1)
    /*
     * SDMC1: PC.0..PC.6, function 2, pull-up, drive 3.
     * PC.0..PC.5 are shared with PWM0..PWM3 on this board, so SDMC1 and
     * PWM must not be enabled together.  PC.6 is DAT3, which some cards
     * also use for card detect; this stage does not register a hot-plug
     * IRQ and does not read it.
     * Source: Luban-Lite target/d21x/demo128-nand/pinmux.c (Apache-2.0).
     */
    aic_pin_cfg(2, 0, AIC_GPIO_PIN_FUNC(2) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_UP));
    aic_pin_cfg(2, 1, AIC_GPIO_PIN_FUNC(2) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_UP));
    aic_pin_cfg(2, 2, AIC_GPIO_PIN_FUNC(2) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_UP));
    aic_pin_cfg(2, 3, AIC_GPIO_PIN_FUNC(2) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_UP));
    aic_pin_cfg(2, 4, AIC_GPIO_PIN_FUNC(2) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_UP));
    aic_pin_cfg(2, 5, AIC_GPIO_PIN_FUNC(2) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_UP));
    aic_pin_cfg(2, 6, AIC_GPIO_PIN_FUNC(2) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_UP));
#endif

#if defined(BSP_USING_SDMC2)
    /*
     * SDMC2: PF.0..PF.5, function 2, pull-up, drive 3.
     * On the carplay board this group goes to the RTL8733BS SDIO
     * interface.  The BT HCI port is UART7 on PF.6..PF.9, configured
     * below when BSP_USING_UART7 is set.
     * Source: Luban-Lite target/d21x/demo128-nand-carplay/pinmux.c.
     */
    aic_pin_cfg(5, 0, AIC_GPIO_PIN_FUNC(2) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_UP));
    aic_pin_cfg(5, 1, AIC_GPIO_PIN_FUNC(2) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_UP));
    aic_pin_cfg(5, 2, AIC_GPIO_PIN_FUNC(2) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_UP));
    aic_pin_cfg(5, 3, AIC_GPIO_PIN_FUNC(2) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_UP));
    aic_pin_cfg(5, 4, AIC_GPIO_PIN_FUNC(2) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_UP));
    aic_pin_cfg(5, 5, AIC_GPIO_PIN_FUNC(2) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_UP));
#endif

#if defined(BSP_USING_UART7)
    /*
     * UART7: BT HCI on the Wi-Fi/BT combo module (RTL8733BS).
     * PF.6 TX, PF.7 RX, PF.8 RTS, PF.9 CTS, all function 5.
     * Pull-up on the two received lines, matching the vendor board file.
     * Source: Luban-Lite target/d21x/demo128-nand-carplay/pinmux.c.
     */
    aic_pin_cfg(5, 6, AIC_GPIO_PIN_FUNC(5) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(5, 7, AIC_GPIO_PIN_FUNC(5) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_UP));
    aic_pin_cfg(5, 8, AIC_GPIO_PIN_FUNC(5) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(5, 9, AIC_GPIO_PIN_FUNC(5) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_UP));
#endif

#if defined(BSP_USING_PWM)
    /*
     * PWM0..PWM2: PC.0..PC.5, function 7.  PC.6/PC.7 are left alone:
     * PC.7 is the panel enable GPIO used by the display driver, so PWM3
     * must not claim it on this board.
     */
    aic_pin_cfg(2, 0, AIC_GPIO_PIN_FUNC(7) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(2, 1, AIC_GPIO_PIN_FUNC(7) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(2, 2, AIC_GPIO_PIN_FUNC(7) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(2, 3, AIC_GPIO_PIN_FUNC(7) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(2, 4, AIC_GPIO_PIN_FUNC(7) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(2, 5, AIC_GPIO_PIN_FUNC(7) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
#endif

#if defined(BSP_USING_DISPLAY)
#if BSP_DISPLAY_LVDS_LINK == 1
    /* LVDS link 1: PD.8..PD.17, function 3. */
    aic_pin_cfg(3, 8,  AIC_GPIO_PIN_FUNC(3) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 9,  AIC_GPIO_PIN_FUNC(3) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 10, AIC_GPIO_PIN_FUNC(3) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 11, AIC_GPIO_PIN_FUNC(3) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 12, AIC_GPIO_PIN_FUNC(3) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 13, AIC_GPIO_PIN_FUNC(3) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 14, AIC_GPIO_PIN_FUNC(3) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 15, AIC_GPIO_PIN_FUNC(3) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 16, AIC_GPIO_PIN_FUNC(3) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 17, AIC_GPIO_PIN_FUNC(3) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
#else
    /* LVDS link 0: PD.18..PD.27, function 3. */
    aic_pin_cfg(3, 18, AIC_GPIO_PIN_FUNC(3) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 19, AIC_GPIO_PIN_FUNC(3) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 20, AIC_GPIO_PIN_FUNC(3) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 21, AIC_GPIO_PIN_FUNC(3) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 22, AIC_GPIO_PIN_FUNC(3) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 23, AIC_GPIO_PIN_FUNC(3) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 24, AIC_GPIO_PIN_FUNC(3) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 25, AIC_GPIO_PIN_FUNC(3) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 26, AIC_GPIO_PIN_FUNC(3) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(3, 27, AIC_GPIO_PIN_FUNC(3) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
#endif
#endif

#if defined(BSP_USING_AUDIO)
    /*
     * Audio: PE.11 is SPK1 (function 4) on the carplay board, driving the
     * on-board speaker amplifier; PA.7 is the amplifier shutdown line
     * (PA_SHDN, active low, function 1 = GPIO).  PA.7 is left as an
     * output driven high (amplifier off) until the audio driver enables
     * it.  Source: Luban-Lite target/d21x/demo128-nand-carplay/pinmux.c.
     */
    aic_pin_cfg(4, 11, AIC_GPIO_PIN_FUNC(4) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(0, 7, AIC_GPIO_PIN_FUNC(1) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
#endif

#if defined(BSP_USING_DMIC)
    /*
     * DMIC: PE.12 is DMIC_CLK and PE.13 is DMIC_D0 (function 4).
     * The codec generates the bit clock and decimates the data line.
     * Source: D213ECV-DEMO-V4-0 hardware manual (DMIC section) and the
     * D21x GPIO function table (PE12_DMIC_CLK, PE13_DMIC_D0).
     */
    aic_pin_cfg(4, 12, AIC_GPIO_PIN_FUNC(4) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(4, 13, AIC_GPIO_PIN_FUNC(4) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
#endif

#if defined(BSP_USING_USB0_HOST) || defined(BSP_USING_USB0_DEVICE)
    /*
     * USB0 D+/D-: PO.0 / PO.1, function 2, drive 3, no pull.
     * The role is selected by the syscfg USB0_CFG.DRD_MODE bit: the
     * CherryUSB host glue clears it, the device port sets it.
     * Source: Luban-Lite target/d21x/demo128-nand-carplay/pinmux.c.
     */
    aic_pin_cfg(14, 0, AIC_GPIO_PIN_FUNC(2) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(14, 1, AIC_GPIO_PIN_FUNC(2) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
#endif

#if defined(BSP_USING_USB1_HOST)
    /*
     * USB1 D+/D-: PO.2 / PO.3, function 2, drive 3, no pull.  USB1 is
     * host-only on D21x and has no PHY role bit.
     * Source: Luban-Lite target/d21x/demo128-nand/pinmux.c.
     */
    aic_pin_cfg(14, 2, AIC_GPIO_PIN_FUNC(2) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(14, 3, AIC_GPIO_PIN_FUNC(2) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
#endif

#if defined(BSP_USING_GMAC0)
    /*
     * GMAC0 RMII: PE.0..PE.5 and PE.7..PE.9, function 6, drive 3.
     * PE.6 is the PHY reset line, kept as GPIO (function 1); the MAC HAL
     * drives it through mac_config[].phyrst_gpio_name.
     * Source: Luban-Lite target/d21x/demo128-nand/pinmux.c.
     */
    aic_pin_cfg(4, 0, AIC_GPIO_PIN_FUNC(6) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(4, 1, AIC_GPIO_PIN_FUNC(6) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(4, 2, AIC_GPIO_PIN_FUNC(6) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(4, 3, AIC_GPIO_PIN_FUNC(6) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(4, 4, AIC_GPIO_PIN_FUNC(6) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(4, 5, AIC_GPIO_PIN_FUNC(6) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(4, 6, AIC_GPIO_PIN_FUNC(1) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(4, 7, AIC_GPIO_PIN_FUNC(6) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(4, 8, AIC_GPIO_PIN_FUNC(6) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
    aic_pin_cfg(4, 9, AIC_GPIO_PIN_FUNC(6) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));

    /*
     * PE.10 = CLK_OUT2, 25 MHz clock to the RTL8201F XTAL_IN.  The PHY
     * does not respond on MDIO until this clock is present.
     * Source: D213ECV-DEMO-V4-0 schematic and hardware manual (U15-32).
     */
    aic_pin_cfg(4, 10, AIC_GPIO_PIN_FUNC(6) | AIC_GPIO_PIN_DRIVE(3) | AIC_GPIO_PIN_PULL(PIN_PULL_DIS));
#endif

    /*
     * UART3/5/6 are not routed to a connector on the D213ECV-DEMO-V4-0 board and
     * the vendor board file does not define pinmux for them.  If a product
     * uses one of these instances, add the board-specific pinmux here.
     */
}
