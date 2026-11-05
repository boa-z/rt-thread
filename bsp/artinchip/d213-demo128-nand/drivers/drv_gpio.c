/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D21x GPIO / pinmux driver.
 *
 * The register programming is done by the vendor HAL
 * (vendor/hal/aic_hal_gpio.c).  This file adapts it to the standard
 * RT-Thread pin device and dispatches the eight GPIO group interrupts.
 *
 * Pin number encoding: group * 32 + pin, group 'A' == 0.
 * Examples: PA.0 == 0, PC.7 == 71, PE.16 == 144.
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "aic_hal_gpio.h"

/* D21x exposes PA..PG.  The PLIC reserves eight group lines (68..75). */
#define AIC_GPIO_GROUP_NUM      7
#define AIC_GPIO_PINS_PER_GROUP 32
#define AIC_GPIO_PIN_TOTAL      (AIC_GPIO_GROUP_NUM * AIC_GPIO_PINS_PER_GROUP)

struct aic_gpio_irq_slot
{
    void (*handler)(void *args);
    void *args;
};

static struct aic_gpio_irq_slot aic_gpio_irq[AIC_GPIO_GROUP_NUM][AIC_GPIO_PINS_PER_GROUP];
static rt_uint8_t aic_gpio_group_installed;

static rt_bool_t aic_gpio_pin_valid(rt_base_t pin)
{
    return (pin >= 0) && (pin < AIC_GPIO_PIN_TOTAL);
}

static rt_base_t aic_gpio_name2pin(const char *name)
{
    rt_base_t group;
    rt_base_t pin = 0;
    const char *p;

    if (name == RT_NULL || name[0] != 'P' || name[2] != '.')
        return -RT_EINVAL;

    if (name[1] < 'A' || name[1] >= ('A' + AIC_GPIO_GROUP_NUM))
        return -RT_EINVAL;

    group = name[1] - 'A';
    for (p = &name[3]; *p; p++)
    {
        if (*p < '0' || *p > '9')
            return -RT_EINVAL;
        pin = pin * 10 + (*p - '0');
    }

    if (pin >= AIC_GPIO_PINS_PER_GROUP)
        return -RT_EINVAL;

    return group * AIC_GPIO_PINS_PER_GROUP + pin;
}

static void aic_gpio_mode(struct rt_device *device, rt_base_t pin, rt_uint8_t mode)
{
    rt_base_t group;
    rt_base_t offset;

    (void)device;
    if (!aic_gpio_pin_valid(pin))
        return;

    group = GPIO_GROUP(pin);
    offset = GPIO_GROUP_PIN(pin);

    /* Function 1 selects the GPIO controller. */
    hal_gpio_set_func(group, offset, 1);

    switch (mode)
    {
    case PIN_MODE_INPUT:
        hal_gpio_set_bias_pull(group, offset, PIN_PULL_DIS);
        hal_gpio_direction_input(group, offset);
        break;
    case PIN_MODE_INPUT_PULLUP:
        hal_gpio_set_bias_pull(group, offset, PIN_PULL_UP);
        hal_gpio_direction_input(group, offset);
        break;
    case PIN_MODE_INPUT_PULLDOWN:
        hal_gpio_set_bias_pull(group, offset, PIN_PULL_DOWN);
        hal_gpio_direction_input(group, offset);
        break;
    case PIN_MODE_OUTPUT:
    case PIN_MODE_OUTPUT_OD:
    default:
        hal_gpio_set_bias_pull(group, offset, PIN_PULL_DIS);
        hal_gpio_direction_output(group, offset);
        break;
    }
}

static void aic_gpio_write(struct rt_device *device, rt_base_t pin, rt_uint8_t value)
{
    rt_base_t group;
    rt_base_t offset;

    (void)device;
    if (!aic_gpio_pin_valid(pin))
        return;

    group = GPIO_GROUP(pin);
    offset = GPIO_GROUP_PIN(pin);

    if (value == PIN_LOW)
        hal_gpio_clr_output(group, offset);
    else
        hal_gpio_set_output(group, offset);
}

static rt_ssize_t aic_gpio_read(struct rt_device *device, rt_base_t pin)
{
    rt_base_t group;
    rt_base_t offset;
    unsigned int value = 0;

    (void)device;
    if (!aic_gpio_pin_valid(pin))
        return -RT_EINVAL;

    group = GPIO_GROUP(pin);
    offset = GPIO_GROUP_PIN(pin);
    hal_gpio_get_value(group, offset, &value);

    return value;
}

static void aic_gpio_group_isr(int irq, void *param)
{
    rt_base_t group = irq - GPIO_IRQn;
    unsigned int stat = 0;
    unsigned int en = 0;
    unsigned int pending;
    int bit;

    (void)param;
    if (group < 0 || group >= AIC_GPIO_GROUP_NUM)
        return;

    hal_gpio_group_get_irq_stat(group, &stat);
    hal_gpio_group_get_irq_en(group, &en);
    pending = stat & en;

    for (bit = 0; bit < AIC_GPIO_PINS_PER_GROUP; bit++)
    {
        if (!(pending & (1U << bit)))
            continue;

        /* W1C before invoking the handler so a new edge is latched. */
        hal_gpio_clr_irq_stat(group, bit);

        if (aic_gpio_irq[group][bit].handler)
            aic_gpio_irq[group][bit].handler(aic_gpio_irq[group][bit].args);
    }
}

static rt_err_t aic_gpio_attach_irq(struct rt_device *device, rt_base_t pin,
                                    rt_uint8_t mode,
                                    void (*hdr)(void *args), void *args)
{
    rt_base_t group;
    rt_base_t offset;
    unsigned int hw_mode;

    (void)device;
    if (!aic_gpio_pin_valid(pin) || hdr == RT_NULL)
        return -RT_EINVAL;

    group = GPIO_GROUP(pin);
    offset = GPIO_GROUP_PIN(pin);

    switch (mode)
    {
    case PIN_IRQ_MODE_RISING:
        hw_mode = PIN_IRQ_MODE_EDGE_RISING;
        break;
    case PIN_IRQ_MODE_FALLING:
        hw_mode = PIN_IRQ_MODE_EDGE_FALLING;
        break;
    case PIN_IRQ_MODE_RISING_FALLING:
        hw_mode = PIN_IRQ_MODE_EDGE_BOTH;
        break;
    case PIN_IRQ_MODE_HIGH_LEVEL:
        hw_mode = PIN_IRQ_MODE_LEVEL_HIGH;
        break;
    case PIN_IRQ_MODE_LOW_LEVEL:
        hw_mode = PIN_IRQ_MODE_LEVEL_LOW;
        break;
    default:
        return -RT_EINVAL;
    }

    aic_gpio_irq[group][offset].handler = hdr;
    aic_gpio_irq[group][offset].args = args;

    hal_gpio_set_irq_mode(group, offset, hw_mode);
    hal_gpio_clr_irq_stat(group, offset);

    if (!(aic_gpio_group_installed & (1U << group)))
    {
        rt_hw_interrupt_install(GPIO_IRQn + group, aic_gpio_group_isr,
                                RT_NULL, "gpio");
        aic_gpio_group_installed |= (1U << group);
    }

    return RT_EOK;
}

static rt_err_t aic_gpio_detach_irq(struct rt_device *device, rt_base_t pin)
{
    rt_base_t group;
    rt_base_t offset;

    (void)device;
    if (!aic_gpio_pin_valid(pin))
        return -RT_EINVAL;

    group = GPIO_GROUP(pin);
    offset = GPIO_GROUP_PIN(pin);

    hal_gpio_disable_irq(group, offset);
    aic_gpio_irq[group][offset].handler = RT_NULL;
    aic_gpio_irq[group][offset].args = RT_NULL;

    return RT_EOK;
}

static rt_err_t aic_gpio_irq_enable(struct rt_device *device, rt_base_t pin,
                                    rt_uint8_t enabled)
{
    rt_base_t group;
    rt_base_t offset;

    (void)device;
    if (!aic_gpio_pin_valid(pin))
        return -RT_EINVAL;

    group = GPIO_GROUP(pin);
    offset = GPIO_GROUP_PIN(pin);

    if (enabled)
    {
        if (aic_gpio_irq[group][offset].handler == RT_NULL)
            return -RT_EINVAL;
        hal_gpio_enable_irq(group, offset);
        rt_hw_interrupt_umask(GPIO_IRQn + group);
    }
    else
    {
        hal_gpio_disable_irq(group, offset);
    }

    return RT_EOK;
}

static rt_err_t aic_gpio_debounce(struct rt_device *device, rt_base_t pin,
                                  rt_uint32_t debounce)
{
    rt_base_t group;
    rt_base_t offset;

    (void)device;
    if (!aic_gpio_pin_valid(pin) || debounce > 0xFFF)
        return -RT_EINVAL;

    group = GPIO_GROUP(pin);
    offset = GPIO_GROUP_PIN(pin);
    hal_gpio_set_debounce(group, offset, debounce);

    return RT_EOK;
}

static const struct rt_pin_ops aic_gpio_ops =
{
    .pin_mode = aic_gpio_mode,
    .pin_write = aic_gpio_write,
    .pin_read = aic_gpio_read,
    .pin_attach_irq = aic_gpio_attach_irq,
    .pin_detach_irq = aic_gpio_detach_irq,
    .pin_irq_enable = aic_gpio_irq_enable,
    .pin_get = aic_gpio_name2pin,
    .pin_debounce = aic_gpio_debounce,
};

int rt_hw_gpio_init(void)
{
    return rt_device_pin_register("gpio", &aic_gpio_ops, RT_NULL);
}
INIT_BOARD_EXPORT(rt_hw_gpio_init);
