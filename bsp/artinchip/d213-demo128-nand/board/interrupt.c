/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D21x PLIC (machine-mode) interrupt controller.
 *
 * The D21x PLIC is a T-Head/C906 compatible PLIC with a non-standard
 * register layout.  The offsets used here are from the vendor
 * core_rv64.h and vectors.S.
 */

#include <rthw.h>
#include <rtthread.h>
#include <stdint.h>

#include "board.h"

#define AIC_PLIC_PRIO_BASE   (AIC_PLIC_BASE + 0x0000000UL)
#define AIC_PLIC_ENABLE_BASE (AIC_PLIC_BASE + 0x0002000UL)
#define AIC_PLIC_PER         (AIC_PLIC_BASE + 0x01FFFFCUL)
#define AIC_PLIC_THRESHOLD   (AIC_PLIC_BASE + 0x0200000UL)
#define AIC_PLIC_CLAIM       (AIC_PLIC_BASE + 0x0200004UL)

#define AIC_PLIC_MAX_IRQ 128

#define AIC_MIE_MTIE (1UL << 7)
#define AIC_MIE_MEIE (1UL << 11)

static struct rt_irq_desc aic_irq_table[AIC_PLIC_MAX_IRQ];

static inline uint32_t aic_readl(uint32_t addr)
{
    return *(volatile uint32_t *)(uintptr_t)addr;
}

static inline void aic_writel(uint32_t value, uint32_t addr)
{
    *(volatile uint32_t *)(uintptr_t)addr = value;
}

static void aic_irq_default_handler(int vector, void *param)
{
    rt_kprintf("Unhandled IRQ %d\n", vector);
}

static void aic_plic_enable(int irq)
{
    uint32_t addr;

    if (irq <= 0 || irq >= AIC_PLIC_MAX_IRQ)
    {
        return;
    }

    addr = AIC_PLIC_ENABLE_BASE + (irq / 32) * 4;
    aic_writel(aic_readl(addr) | (1U << (irq % 32)), addr);
}

static void aic_plic_disable(int irq)
{
    uint32_t addr;

    if (irq <= 0 || irq >= AIC_PLIC_MAX_IRQ)
    {
        return;
    }

    addr = AIC_PLIC_ENABLE_BASE + (irq / 32) * 4;
    aic_writel(aic_readl(addr) & ~(1U << (irq % 32)), addr);
}

void rt_hw_interrupt_init(void)
{
    int i;

    for (i = 1; i < AIC_PLIC_MAX_IRQ; i++)
    {
        aic_writel(1, AIC_PLIC_PRIO_BASE + i * 4);
    }

    for (i = 0; i < AIC_PLIC_MAX_IRQ / 32; i++)
    {
        aic_writel(0, AIC_PLIC_ENABLE_BASE + i * 4);
    }

    /* Enable the PLIC priority function and set threshold 0. */
    aic_writel(1, AIC_PLIC_PER);
    aic_writel(0, AIC_PLIC_THRESHOLD);

    for (i = 0; i < AIC_PLIC_MAX_IRQ; i++)
    {
        aic_irq_table[i].handler = aic_irq_default_handler;
        aic_irq_table[i].param = RT_NULL;
#ifdef RT_USING_INTERRUPT_INFO
        rt_strncpy(aic_irq_table[i].name, "unknown", RT_NAME_MAX);
        aic_irq_table[i].counter = 0;
#endif
    }

    /* Enable machine external and machine timer interrupts. */
    __asm__ volatile("csrs mie, %0" : : "r"(AIC_MIE_MEIE | AIC_MIE_MTIE));
}

void rt_hw_interrupt_mask(int vector)
{
    aic_plic_disable(vector);
}

void rt_hw_interrupt_umask(int vector)
{
    aic_plic_enable(vector);
}

rt_isr_handler_t rt_hw_interrupt_install(int vector,
                                         rt_isr_handler_t handler,
                                         void *param,
                                         const char *name)
{
    rt_isr_handler_t old_handler;

    if (vector <= 0 || vector >= AIC_PLIC_MAX_IRQ || handler == RT_NULL)
    {
        return RT_NULL;
    }

    old_handler = aic_irq_table[vector].handler;
    aic_irq_table[vector].handler = handler;
    aic_irq_table[vector].param = param;

#ifdef RT_USING_INTERRUPT_INFO
    if (name)
    {
        rt_strncpy(aic_irq_table[vector].name, name, RT_NAME_MAX);
    }
    aic_irq_table[vector].counter = 0;
#else
    (void)name;
#endif

    return old_handler;
}

void rt_hw_interrupt_uninstall(int vector,
                               rt_isr_handler_t handler,
                               void *param)
{
    if (vector <= 0 || vector >= AIC_PLIC_MAX_IRQ)
    {
        return;
    }

    if (aic_irq_table[vector].handler != handler ||
        aic_irq_table[vector].param != param)
    {
        return;
    }

    aic_irq_table[vector].handler = aic_irq_default_handler;
    aic_irq_table[vector].param = RT_NULL;
}

/*
 * Called from libcpu/risc-v/rv64/trap.c for a machine external interrupt.
 */
void rt_hw_irq_isr(void)
{
    uint32_t irq;
    struct rt_irq_desc *desc;

    irq = aic_readl(AIC_PLIC_CLAIM);
    if (irq == 0 || irq >= AIC_PLIC_MAX_IRQ)
    {
        return;
    }

    desc = &aic_irq_table[irq];
    if (desc->handler)
    {
        desc->handler(irq, desc->param);
    }

    /* Complete the claim; the PLIC deasserts the external interrupt. */
    aic_writel(irq, AIC_PLIC_CLAIM);
}
