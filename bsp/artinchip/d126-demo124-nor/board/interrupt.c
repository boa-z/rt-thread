/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D12p CLIC interrupt controller.
 *
 * The D12p E907F uses a CLIC at 0x20800000.  External interrupt
 * vectors are dispatched through the mtvt table; the table entries all
 * point at Default_IRQHandler except vector 3 (PendSV_Handler).
 */

#include <rthw.h>
#include <rtthread.h>
#include <stdint.h>

#include "board.h"

#define CLIC_CFG        (AIC_CLIC_BASE + 0x0000UL)
#define CLIC_INFO       (AIC_CLIC_BASE + 0x0004UL)
#define CLIC_THRESH     (AIC_CLIC_BASE + 0x0008UL)
#define CLIC_INT_BASE   (AIC_CLIC_BASE + 0x1000UL)

#define CLIC_IP(irq)    (CLIC_INT_BASE + (irq) * 4UL + 0UL)
#define CLIC_IE(irq)    (CLIC_INT_BASE + (irq) * 4UL + 1UL)
#define CLIC_ATTR(irq)  (CLIC_INT_BASE + (irq) * 4UL + 2UL)
#define CLIC_CTL(irq)   (CLIC_INT_BASE + (irq) * 4UL + 3UL)

#define CLIC_IRQ_SOFTWARE   3
#define CLIC_ATTR_VECTOR    0x01
#define CLIC_ATTR_EDGE      0x03

static struct rt_irq_desc aic_irq_table[AIC_CLIC_IRQ_NUM];

static inline uint32_t aic_readl(uint32_t addr)
{
    return *(volatile uint32_t *)(uintptr_t)addr;
}

static inline void aic_writel(uint32_t value, uint32_t addr)
{
    *(volatile uint32_t *)(uintptr_t)addr = value;
}

static inline uint8_t aic_readb(uint32_t addr)
{
    return *(volatile uint8_t *)(uintptr_t)addr;
}

static inline void aic_writeb(uint8_t value, uint32_t addr)
{
    *(volatile uint8_t *)(uintptr_t)addr = value;
}

static void aic_irq_default_handler(int vector, void *param)
{
    rt_kprintf("Unhandled IRQ %d\n", vector);
}

static void aic_irq_enable(int irq)
{
    if (irq < 0 || irq >= AIC_CLIC_IRQ_NUM)
    {
        return;
    }
    aic_writeb(aic_readb(CLIC_IE(irq)) | 0x01, CLIC_IE(irq));
}

static void aic_irq_disable(int irq)
{
    if (irq < 0 || irq >= AIC_CLIC_IRQ_NUM)
    {
        return;
    }
    aic_writeb(aic_readb(CLIC_IE(irq)) & ~0x01, CLIC_IE(irq));
}

void rt_hw_interrupt_init(void)
{
    uint32_t info;
    uint32_t nlbits;
    int i;

    info = aic_readl(CLIC_INFO);
    nlbits = (info >> 21) & 0x0f;
    if (nlbits == 0)
    {
        nlbits = 1;
    }
    aic_writel(nlbits << 1, CLIC_CFG);
    aic_writel(0, CLIC_THRESH);

    for (i = 0; i < AIC_CLIC_IRQ_NUM; i++)
    {
        aic_writeb(0, CLIC_IP(i));
        aic_writeb(0x80, CLIC_CTL(i));
        aic_writeb(CLIC_ATTR_VECTOR, CLIC_ATTR(i));
        aic_writeb(0, CLIC_IE(i));

        aic_irq_table[i].handler = aic_irq_default_handler;
        aic_irq_table[i].param = RT_NULL;
#ifdef RT_USING_INTERRUPT_INFO
        rt_strncpy(aic_irq_table[i].name, "unknown", RT_NAME_MAX);
        aic_irq_table[i].counter = 0;
#endif
    }

    /* The context-switch software interrupt is edge triggered. */
    aic_writeb(CLIC_ATTR_EDGE, CLIC_ATTR(CLIC_IRQ_SOFTWARE));
    aic_writeb(0x80, CLIC_CTL(CLIC_IRQ_SOFTWARE));
    aic_irq_enable(CLIC_IRQ_SOFTWARE);
}

void rt_hw_interrupt_mask(int vector)
{
    aic_irq_disable(vector);
}

void rt_hw_interrupt_umask(int vector)
{
    aic_irq_enable(vector);
}

rt_isr_handler_t rt_hw_interrupt_install(int vector,
                                         rt_isr_handler_t handler,
                                         void *param,
                                         const char *name)
{
    rt_isr_handler_t old_handler;

    if (vector < 0 || vector >= AIC_CLIC_IRQ_NUM || handler == RT_NULL)
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
    if (vector < 0 || vector >= AIC_CLIC_IRQ_NUM)
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

void aic_irq_dispatch(rt_uint32_t irq)
{
    struct rt_irq_desc *desc;

    if (irq >= AIC_CLIC_IRQ_NUM)
    {
        rt_kprintf("Invalid IRQ %u\n", (unsigned)irq);
        return;
    }

    desc = &aic_irq_table[irq];
    if (desc->handler)
    {
        desc->handler((int)irq, desc->param);
    }
}

void aic_exception_handler(rt_ubase_t mcause, rt_ubase_t mepc,
                           rt_ubase_t mtval, rt_ubase_t *regs)
{
    rt_kprintf("\nCPU exception: mcause=0x%08x mepc=0x%08x mtval=0x%08x\n",
               (unsigned)mcause, (unsigned)mepc, (unsigned)mtval);
    rt_kprintf("ra=0x%08x sp=0x%08x gp=0x%08x tp=0x%08x\n",
               (unsigned)regs[0], (unsigned)(regs + 32),
               (unsigned)regs[28], (unsigned)regs[29]);
    rt_kprintf("a0=0x%08x a1=0x%08x a2=0x%08x a3=0x%08x\n",
               (unsigned)regs[8], (unsigned)regs[9],
               (unsigned)regs[10], (unsigned)regs[11]);

    while (1);
}
