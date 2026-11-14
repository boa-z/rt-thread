/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * D126 GPIO tests.
 *
 * PD8 is the RGBLED data line on D126FxR-DEMO-V1-1 (schematic net
 * PD8_RGBLED).  It drives the source of the BSN20 level shifter through
 * a 10k pull-up to VCC_LCD, with a WS2812 chain on the 5 V side, so
 * toggling it is harmless.  UART0 (PA0/PA1) and the CAN0 pins (PA2/PA3)
 * are deliberately not used.
 *
 * PB12 is not routed by the D126FxR-DEMO pinmux, so its pad follows the
 * internal pull resistors.  The driver keeps the input buffer enabled in
 * output mode (DIR = IE|OE), so the same pad can also be driven and read
 * back, which lets the interrupt test generate a real edge on the pad
 * without external wiring.
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "utest.h"

#define GPIO_OUTPUT_PIN     "PD.8"
#define GPIO_IRQ_PIN        "PB.12"
#define GPIO_IRQ_TIMEOUT_MS 500

static struct rt_semaphore gpio_irq_sem;
static volatile rt_uint32_t gpio_irq_count;

static void gpio_irq_callback(void *args)
{
    (void)args;

    gpio_irq_count++;
    rt_sem_release(&gpio_irq_sem);
}

static void tc_gpio_output(void)
{
    rt_base_t pin = rt_pin_get(GPIO_OUTPUT_PIN);
    int i;

    uassert_true(pin >= 0);
    if (pin < 0)
    {
        return;
    }

    rt_pin_mode(pin, PIN_MODE_OUTPUT);
    for (i = 0; i < 3; i++)
    {
        rt_pin_write(pin, PIN_LOW);
        uassert_int_equal(rt_pin_read(pin), PIN_LOW);

        rt_pin_write(pin, PIN_HIGH);
        uassert_int_equal(rt_pin_read(pin), PIN_HIGH);
    }

    /* Idle low: the WS2812 data line needs a low reset period. */
    rt_pin_write(pin, PIN_LOW);
}

static void tc_gpio_input(void)
{
    rt_base_t pin = rt_pin_get(GPIO_IRQ_PIN);

    uassert_true(pin >= 0);
    if (pin < 0)
    {
        return;
    }

    rt_pin_mode(pin, PIN_MODE_INPUT_PULLUP);
    uassert_int_equal(rt_pin_read(pin), PIN_HIGH);

    rt_pin_mode(pin, PIN_MODE_INPUT_PULLDOWN);
    uassert_int_equal(rt_pin_read(pin), PIN_LOW);
}

static void tc_gpio_irq(void)
{
    rt_base_t pin = rt_pin_get(GPIO_IRQ_PIN);
    rt_err_t ret;

    uassert_true(pin >= 0);
    if (pin < 0)
    {
        return;
    }

    rt_pin_mode(pin, PIN_MODE_OUTPUT);
    rt_pin_write(pin, PIN_LOW);
    rt_thread_mdelay(1);

    /* Rising edge: drive the pad from low to high. */
    rt_sem_control(&gpio_irq_sem, RT_IPC_CMD_RESET, RT_NULL);
    gpio_irq_count = 0;
    uassert_int_equal(rt_pin_attach_irq(pin, PIN_IRQ_MODE_RISING,
                                        gpio_irq_callback, RT_NULL),
                      RT_EOK);
    uassert_int_equal(rt_pin_irq_enable(pin, PIN_IRQ_ENABLE), RT_EOK);

    rt_pin_write(pin, PIN_HIGH);
    ret = rt_sem_take(&gpio_irq_sem,
                      rt_tick_from_millisecond(GPIO_IRQ_TIMEOUT_MS));
    uassert_int_equal(ret, RT_EOK);
    uassert_true(gpio_irq_count >= 1);

    rt_pin_irq_enable(pin, PIN_IRQ_DISABLE);
    uassert_int_equal(rt_pin_detach_irq(pin), RT_EOK);

    /* Falling edge: drive the pad from high to low. */
    rt_pin_write(pin, PIN_HIGH);
    rt_thread_mdelay(1);
    rt_sem_control(&gpio_irq_sem, RT_IPC_CMD_RESET, RT_NULL);
    gpio_irq_count = 0;
    uassert_int_equal(rt_pin_attach_irq(pin, PIN_IRQ_MODE_FALLING,
                                        gpio_irq_callback, RT_NULL),
                      RT_EOK);
    uassert_int_equal(rt_pin_irq_enable(pin, PIN_IRQ_ENABLE), RT_EOK);

    rt_pin_write(pin, PIN_LOW);
    ret = rt_sem_take(&gpio_irq_sem,
                      rt_tick_from_millisecond(GPIO_IRQ_TIMEOUT_MS));
    uassert_int_equal(ret, RT_EOK);
    uassert_true(gpio_irq_count >= 1);

    rt_pin_irq_enable(pin, PIN_IRQ_DISABLE);
    uassert_int_equal(rt_pin_detach_irq(pin), RT_EOK);

    rt_pin_mode(pin, PIN_MODE_INPUT_PULLDOWN);
}

static void tc_gpio_irq_masked(void)
{
    rt_base_t pin = rt_pin_get(GPIO_IRQ_PIN);
    rt_base_t level;
    rt_err_t ret;

    uassert_true(pin >= 0);
    if (pin < 0)
    {
        return;
    }

    rt_pin_mode(pin, PIN_MODE_OUTPUT);
    rt_pin_write(pin, PIN_LOW);
    rt_thread_mdelay(1);

    rt_sem_control(&gpio_irq_sem, RT_IPC_CMD_RESET, RT_NULL);
    gpio_irq_count = 0;
    uassert_int_equal(rt_pin_attach_irq(pin, PIN_IRQ_MODE_RISING,
                                        gpio_irq_callback, RT_NULL),
                      RT_EOK);
    uassert_int_equal(rt_pin_irq_enable(pin, PIN_IRQ_ENABLE), RT_EOK);

    /*
     * Clear MIE in front of a real edge: the CLIC keeps it pending and
     * the handler must not run until interrupts are enabled again.
     */
    level = rt_hw_interrupt_disable();
    rt_pin_write(pin, PIN_HIGH);
    uassert_int_equal(gpio_irq_count, 0);
    rt_hw_interrupt_enable(level);

    ret = rt_sem_take(&gpio_irq_sem,
                      rt_tick_from_millisecond(GPIO_IRQ_TIMEOUT_MS));
    uassert_int_equal(ret, RT_EOK);
    uassert_true(gpio_irq_count >= 1);

    rt_pin_irq_enable(pin, PIN_IRQ_DISABLE);
    uassert_int_equal(rt_pin_detach_irq(pin), RT_EOK);

    /* A detached pin must not deliver the next edge either. */
    gpio_irq_count = 0;
    rt_pin_write(pin, PIN_LOW);
    rt_thread_mdelay(5);
    rt_pin_write(pin, PIN_HIGH);
    rt_thread_mdelay(5);
    uassert_int_equal(gpio_irq_count, 0);

    rt_pin_mode(pin, PIN_MODE_INPUT_PULLDOWN);
}

static rt_err_t utest_tc_init(void)
{
    gpio_irq_count = 0;
    rt_sem_init(&gpio_irq_sem, "gpioirq", 0, RT_IPC_FLAG_FIFO);
    return RT_EOK;
}

static rt_err_t utest_tc_cleanup(void)
{
    rt_sem_detach(&gpio_irq_sem);
    return RT_EOK;
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_gpio_output);
    UTEST_UNIT_RUN(tc_gpio_input);
    UTEST_UNIT_RUN(tc_gpio_irq);
    UTEST_UNIT_RUN(tc_gpio_irq_masked);
}
UTEST_TC_EXPORT(testcase, "bsp.d126.gpio",
                utest_tc_init, utest_tc_cleanup, 10);
