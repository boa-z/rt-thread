/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D12p GPIO v1.2 pin driver.
 *
 * The register block is the same generic ArtInChip GPIO layout used by
 * D21x, but D12p exposes PA..PF plus PM/PN.  Only PA..PF have the
 * documented group interrupt lines (68..73), so PM/PN support GPIO
 * read/write but reject interrupt attachment.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <stdint.h>

#include "board.h"

#define AIC_GPIO_GROUP_STRIDE   0x100UL
#define AIC_GPIO_PIN_STRIDE     0x4UL

#define AIC_GPIO_IN_STAT        0x00UL
#define AIC_GPIO_OUT_CFG        0x04UL
#define AIC_GPIO_IRQ_EN         0x08UL
#define AIC_GPIO_IRQ_STAT       0x0cUL
#define AIC_GPIO_OUT_CLR        0x10UL
#define AIC_GPIO_OUT_SET        0x14UL
#define AIC_GPIO_OUT_TOG        0x18UL
#define AIC_GPIO_PIN_CFG        0x80UL

#define AIC_GPIO_FUNC_GPIO      1UL

#define AIC_GPIO_PULL_DIS       0UL
#define AIC_GPIO_PULL_DOWN      2UL
#define AIC_GPIO_PULL_UP        3UL

#define AIC_GPIO_DIR_INPUT      1UL
#define AIC_GPIO_DIR_OUTPUT     2UL

#define AIC_GPIO_IRQ_FALLING    0UL
#define AIC_GPIO_IRQ_RISING     1UL
#define AIC_GPIO_IRQ_LOW        2UL
#define AIC_GPIO_IRQ_HIGH       3UL
#define AIC_GPIO_IRQ_BOTH       4UL

#define AIC_GPIO_GROUP_PA       0
#define AIC_GPIO_GROUP_PF       5
#define AIC_GPIO_GROUP_PM       12
#define AIC_GPIO_GROUP_PN       13
#define AIC_GPIO_GROUP_MAX      14
#define AIC_GPIO_IRQ_FIRST      68
#define AIC_GPIO_IRQ_GROUPS     6

struct aic_gpio_irq_slot
{
    void (*handler)(void *args);
    void *args;
};

static struct aic_gpio_irq_slot
    aic_gpio_irq[AIC_GPIO_IRQ_GROUPS][32];
static rt_uint8_t aic_gpio_group_installed;

static inline uint32_t aic_gpio_read(uint32_t addr)
{
    return *(volatile uint32_t *)(uintptr_t)addr;
}

static inline void aic_gpio_write(uint32_t value, uint32_t addr)
{
    *(volatile uint32_t *)(uintptr_t)addr = value;
}

static uint32_t aic_gpio_gen_addr(rt_base_t group, uint32_t reg)
{
    return AIC_GPIO_BASE + group * AIC_GPIO_GROUP_STRIDE + reg;
}

static uint32_t aic_gpio_cfg_addr(rt_base_t group, rt_base_t pin)
{
    return AIC_GPIO_BASE + group * AIC_GPIO_GROUP_STRIDE +
           AIC_GPIO_PIN_CFG + pin * AIC_GPIO_PIN_STRIDE;
}

static rt_bool_t aic_gpio_group_valid(rt_base_t group)
{
    return (group >= AIC_GPIO_GROUP_PA && group <= AIC_GPIO_GROUP_PF) ||
           group == AIC_GPIO_GROUP_PM || group == AIC_GPIO_GROUP_PN;
}

static rt_bool_t aic_gpio_pin_valid(rt_base_t pin)
{
    rt_base_t group;
    rt_base_t offset;

    if (pin < 0)
    {
        return RT_FALSE;
    }

    group = pin / 32;
    offset = pin % 32;
    return aic_gpio_group_valid(group) && offset < 32;
}

static void aic_gpio_set_func(rt_base_t group, rt_base_t pin, uint32_t func)
{
    uint32_t addr = aic_gpio_cfg_addr(group, pin);
    uint32_t value = aic_gpio_read(addr);

    value &= ~0xfUL;
    value |= func & 0xfUL;
    aic_gpio_write(value, addr);
}

static void aic_gpio_set_pull(rt_base_t group, rt_base_t pin, uint32_t pull)
{
    uint32_t addr = aic_gpio_cfg_addr(group, pin);
    uint32_t value = aic_gpio_read(addr);

    value &= ~(0x3UL << 8);
    value |= (pull & 0x3UL) << 8;
    aic_gpio_write(value, addr);
}

static void aic_gpio_set_dir(rt_base_t group, rt_base_t pin, uint32_t dir)
{
    uint32_t addr = aic_gpio_cfg_addr(group, pin);
    uint32_t value = aic_gpio_read(addr);

    value &= ~(0x3UL << 16);
    value |= (dir & 0x3UL) << 16;
    aic_gpio_write(value, addr);
}

static void aic_gpio_set_irq_mode(rt_base_t group, rt_base_t pin,
                                  uint32_t mode)
{
    uint32_t addr = aic_gpio_cfg_addr(group, pin);
    uint32_t value = aic_gpio_read(addr);

    value &= ~(0x7UL << 12);
    value |= (mode & 0x7UL) << 12;
    aic_gpio_write(value, addr);
}

static void aic_gpio_mode(struct rt_device *device, rt_base_t pin,
                          rt_uint8_t mode)
{
    rt_base_t group;
    rt_base_t offset;

    (void)device;
    if (!aic_gpio_pin_valid(pin))
    {
        return;
    }

    group = pin / 32;
    offset = pin % 32;

    aic_gpio_set_func(group, offset, AIC_GPIO_FUNC_GPIO);
    aic_gpio_set_irq_mode(group, offset, 0);

    switch (mode)
    {
    case PIN_MODE_INPUT:
        aic_gpio_set_pull(group, offset, AIC_GPIO_PULL_DIS);
        aic_gpio_set_dir(group, offset, AIC_GPIO_DIR_INPUT);
        break;
    case PIN_MODE_INPUT_PULLUP:
        aic_gpio_set_pull(group, offset, AIC_GPIO_PULL_UP);
        aic_gpio_set_dir(group, offset, AIC_GPIO_DIR_INPUT);
        break;
    case PIN_MODE_INPUT_PULLDOWN:
        aic_gpio_set_pull(group, offset, AIC_GPIO_PULL_DOWN);
        aic_gpio_set_dir(group, offset, AIC_GPIO_DIR_INPUT);
        break;
    case PIN_MODE_OUTPUT:
    case PIN_MODE_OUTPUT_OD:
    default:
        aic_gpio_write(1UL << offset, aic_gpio_gen_addr(group, AIC_GPIO_OUT_CLR));
        aic_gpio_set_pull(group, offset, AIC_GPIO_PULL_DIS);
        aic_gpio_set_dir(group, offset, AIC_GPIO_DIR_OUTPUT);
        break;
    }
}

static void aic_gpio_write_pin(struct rt_device *device, rt_base_t pin,
                               rt_uint8_t value)
{
    rt_base_t group;
    rt_base_t offset;

    (void)device;
    if (!aic_gpio_pin_valid(pin))
    {
        return;
    }

    group = pin / 32;
    offset = pin % 32;

    if (value == PIN_LOW)
    {
        aic_gpio_write(1UL << offset,
                       aic_gpio_gen_addr(group, AIC_GPIO_OUT_CLR));
    }
    else
    {
        aic_gpio_write(1UL << offset,
                       aic_gpio_gen_addr(group, AIC_GPIO_OUT_SET));
    }
}

static rt_ssize_t aic_gpio_read_pin(struct rt_device *device, rt_base_t pin)
{
    rt_base_t group;
    rt_base_t offset;
    uint32_t value;

    (void)device;
    if (!aic_gpio_pin_valid(pin))
    {
        return -RT_EINVAL;
    }

    group = pin / 32;
    offset = pin % 32;
    value = aic_gpio_read(aic_gpio_gen_addr(group, AIC_GPIO_IN_STAT));

    return (value & (1UL << offset)) ? PIN_HIGH : PIN_LOW;
}

static void aic_gpio_group_isr(int irq, void *param)
{
    rt_base_t group = irq - AIC_GPIO_IRQ_FIRST;
    uint32_t stat;
    uint32_t en;
    uint32_t pending;
    int bit;

    (void)param;
    if (group < 0 || group >= AIC_GPIO_IRQ_GROUPS)
    {
        return;
    }

    stat = aic_gpio_read(aic_gpio_gen_addr(group, AIC_GPIO_IRQ_STAT));
    en = aic_gpio_read(aic_gpio_gen_addr(group, AIC_GPIO_IRQ_EN));
    pending = stat & en;

    for (bit = 0; bit < 32; bit++)
    {
        if (!(pending & (1UL << bit)))
        {
            continue;
        }

        /* W1C before invoking the handler so a new edge is latched. */
        aic_gpio_write(1UL << bit,
                       aic_gpio_gen_addr(group, AIC_GPIO_IRQ_STAT));

        if (aic_gpio_irq[group][bit].handler)
        {
            aic_gpio_irq[group][bit].handler(aic_gpio_irq[group][bit].args);
        }
    }
}

static rt_err_t aic_gpio_attach_irq(struct rt_device *device, rt_base_t pin,
                                    rt_uint8_t mode,
                                    void (*hdr)(void *args), void *args)
{
    rt_base_t group;
    rt_base_t offset;
    uint32_t hw_mode;

    (void)device;
    if (!aic_gpio_pin_valid(pin) || hdr == RT_NULL)
    {
        return -RT_EINVAL;
    }

    group = pin / 32;
    offset = pin % 32;
    if (group > AIC_GPIO_GROUP_PF)
    {
        return -RT_ENOSYS;
    }

    switch (mode)
    {
    case PIN_IRQ_MODE_RISING:
        hw_mode = AIC_GPIO_IRQ_RISING;
        break;
    case PIN_IRQ_MODE_FALLING:
        hw_mode = AIC_GPIO_IRQ_FALLING;
        break;
    case PIN_IRQ_MODE_RISING_FALLING:
        hw_mode = AIC_GPIO_IRQ_BOTH;
        break;
    case PIN_IRQ_MODE_HIGH_LEVEL:
        hw_mode = AIC_GPIO_IRQ_HIGH;
        break;
    case PIN_IRQ_MODE_LOW_LEVEL:
        hw_mode = AIC_GPIO_IRQ_LOW;
        break;
    default:
        return -RT_EINVAL;
    }

    aic_gpio_irq[group][offset].handler = hdr;
    aic_gpio_irq[group][offset].args = args;

    aic_gpio_set_func(group, offset, AIC_GPIO_FUNC_GPIO);
    aic_gpio_set_irq_mode(group, offset, hw_mode);
    aic_gpio_write(1UL << offset,
                   aic_gpio_gen_addr(group, AIC_GPIO_IRQ_STAT));

    if (!(aic_gpio_group_installed & (1U << group)))
    {
        rt_hw_interrupt_install(AIC_GPIO_IRQ_FIRST + group,
                                aic_gpio_group_isr, RT_NULL, "gpio");
        aic_gpio_group_installed |= (1U << group);
    }

    return RT_EOK;
}

static rt_err_t aic_gpio_detach_irq(struct rt_device *device, rt_base_t pin)
{
    rt_base_t group;
    rt_base_t offset;
    uint32_t value;

    (void)device;
    if (!aic_gpio_pin_valid(pin))
    {
        return -RT_EINVAL;
    }

    group = pin / 32;
    offset = pin % 32;
    if (group > AIC_GPIO_GROUP_PF)
    {
        return -RT_ENOSYS;
    }

    aic_gpio_irq[group][offset].handler = RT_NULL;
    aic_gpio_irq[group][offset].args = RT_NULL;
    aic_gpio_set_irq_mode(group, offset, 0);

    value = aic_gpio_read(aic_gpio_gen_addr(group, AIC_GPIO_IRQ_EN));
    value &= ~(1UL << offset);
    aic_gpio_write(value, aic_gpio_gen_addr(group, AIC_GPIO_IRQ_EN));

    return RT_EOK;
}

static rt_err_t aic_gpio_irq_enable(struct rt_device *device, rt_base_t pin,
                                    rt_uint8_t enabled)
{
    rt_base_t group;
    rt_base_t offset;
    uint32_t value;

    (void)device;
    if (!aic_gpio_pin_valid(pin))
    {
        return -RT_EINVAL;
    }

    group = pin / 32;
    offset = pin % 32;
    if (group > AIC_GPIO_GROUP_PF)
    {
        return -RT_ENOSYS;
    }

    value = aic_gpio_read(aic_gpio_gen_addr(group, AIC_GPIO_IRQ_EN));
    if (enabled)
    {
        value |= (1UL << offset);
        rt_hw_interrupt_umask(AIC_GPIO_IRQ_FIRST + group);
    }
    else
    {
        value &= ~(1UL << offset);
    }
    aic_gpio_write(value, aic_gpio_gen_addr(group, AIC_GPIO_IRQ_EN));

    return RT_EOK;
}

static rt_base_t aic_gpio_pin_get(const char *name)
{
    rt_base_t group;
    rt_base_t offset = 0;
    const char *p;

    if (name == RT_NULL || name[0] != 'P' || name[2] != '.')
    {
        return -RT_EINVAL;
    }

    if (name[1] >= 'A' && name[1] <= 'F')
    {
        group = name[1] - 'A';
    }
    else if (name[1] == 'M')
    {
        group = AIC_GPIO_GROUP_PM;
    }
    else if (name[1] == 'S')
    {
        /* PS is a hardware alias for PM on D12p. */
        group = AIC_GPIO_GROUP_PM;
    }
    else if (name[1] == 'N')
    {
        group = AIC_GPIO_GROUP_PN;
    }
    else
    {
        return -RT_EINVAL;
    }

    for (p = &name[3]; *p; p++)
    {
        if (*p < '0' || *p > '9')
        {
            return -RT_EINVAL;
        }
        offset = offset * 10 + (*p - '0');
    }

    if (offset >= 32)
    {
        return -RT_EINVAL;
    }

    return group * 32 + offset;
}

static rt_err_t aic_gpio_debounce(struct rt_device *device, rt_base_t pin,
                                  rt_uint32_t debounce)
{
    rt_base_t group;
    rt_base_t offset;
    uint32_t addr;
    uint32_t value;

    (void)device;
    if (!aic_gpio_pin_valid(pin))
    {
        return -RT_EINVAL;
    }

    group = pin / 32;
    offset = pin % 32;
    addr = aic_gpio_cfg_addr(group, offset);
    value = aic_gpio_read(addr);
    value &= ~(0xfffUL << 20);
    value |= ((debounce & 0xfffUL) << 20);
    aic_gpio_write(value, addr);

    return RT_EOK;
}

static const struct rt_pin_ops aic_gpio_pin_ops =
{
    .pin_mode = aic_gpio_mode,
    .pin_write = aic_gpio_write_pin,
    .pin_read = aic_gpio_read_pin,
    .pin_attach_irq = aic_gpio_attach_irq,
    .pin_detach_irq = aic_gpio_detach_irq,
    .pin_irq_enable = aic_gpio_irq_enable,
    .pin_get = aic_gpio_pin_get,
    .pin_debounce = aic_gpio_debounce,
};

static int aic_gpio_init(void)
{
    return rt_device_pin_register("gpio", &aic_gpio_pin_ops, RT_NULL);
}
INIT_BOARD_EXPORT(aic_gpio_init);
