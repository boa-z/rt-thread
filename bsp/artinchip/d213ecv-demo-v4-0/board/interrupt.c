/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D21x PLIC platform hooks for the RT-Thread RISC-V common
 * interrupt controller.
 *
 * The common controller owns irq_desc[], rt_hw_interrupt_install(),
 * mask/umask and PLIC claim/complete.  This file only provides the
 * ArtInChip MMIO description and the D21x-specific PLIC priority
 * enable (PER) register.
 */

#include <rthw.h>
#include <rtthread.h>
#include <interrupt.h>
#include <stdint.h>

#include "board.h"

#define AIC_PLIC_PER      (AIC_PLIC_BASE + 0x01FFFFCUL)
#define AIC_PLIC_MIE      (AIC_PLIC_BASE + 0x0002000UL)
#define AIC_PLIC_SIE      (AIC_PLIC_BASE + 0x0002080UL)

rt_ubase_t platform_get_plic_base(void)
{
    return AIC_PLIC_BASE;
}

rt_size_t platform_get_plic_size(void)
{
    /* priority + enable + M-mode context 0 (threshold/claim). */
    return 0x210000UL;
}

rt_ubase_t platform_get_plic_quirks(void)
{
    return 0;
}

/*
 * D21x needs the vendor PER bit set before the PLIC priority function
 * is active.  Called from rt_hw_board_init() before rt_hw_interrupt_init().
 */
void aic_plic_quirk_init(void)
{
    volatile uint32_t *mie = (volatile uint32_t *)(uintptr_t)AIC_PLIC_MIE;
    volatile uint32_t *sie = (volatile uint32_t *)(uintptr_t)AIC_PLIC_SIE;
    uint32_t i;

    /* Mask all loader-owned sources before the common PLIC setup. Pending
     * registers are read-only; only complete IDs actually claimed by the
     * common handler. Do not overwrite its default priorities. */
    for (i = 0; i < 32; i++)
    {
        mie[i] = 0;
        sie[i] = 0;
    }

    *(volatile uint32_t *)(uintptr_t)AIC_PLIC_PER = 1;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}
