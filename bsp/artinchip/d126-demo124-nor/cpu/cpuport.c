/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * T-Head E9xx RV32 CPU port for the D126 BSP.
 */

#include <rthw.h>
#include <rtthread.h>

#include "cpuport.h"

#ifndef RT_USING_SMP
volatile rt_ubase_t  rt_interrupt_from_thread = 0;
volatile rt_ubase_t  rt_interrupt_to_thread   = 0;
volatile rt_uint32_t rt_thread_switch_interrupt_flag = 0;
#endif

#define RISCV_RT_STACK_ALIGN16 16
#define RT_HW_FRAME_WORDS      64

/*
 * Frame layout shared with context_gcc.S:
 *
 *   0      mepc
 *   1      x1/ra
 *   2      mstatus
 *   3      x3/gp (unused, gp is fixed)
 *   4..31  x4..x31
 *   32..63 f0..f31
 */
rt_uint8_t *rt_hw_stack_init(void       *tentry,
                             void       *parameter,
                             rt_uint8_t *stack_addr,
                             void       *texit)
{
    rt_ubase_t *frame;
    rt_uint8_t *stk;
    int i;

    stk = stack_addr + sizeof(rt_ubase_t);
    stk = (rt_uint8_t *)RT_ALIGN_DOWN((rt_ubase_t)stk, RISCV_RT_STACK_ALIGN16);
    stk -= sizeof(rt_ubase_t) * RT_HW_FRAME_WORDS;

    frame = (rt_ubase_t *)stk;
    for (i = 0; i < RT_HW_FRAME_WORDS; i++)
    {
        frame[i] = 0;
    }

    frame[0] = (rt_ubase_t)tentry;
    frame[1] = (rt_ubase_t)texit;
    /* MPP = machine mode, MPIE = 1, FS = dirty (FPU enabled). */
    frame[2] = 0x7880;
    frame[10] = (rt_ubase_t)parameter;

    return stk;
}

/** reset CPU */
rt_weak void rt_hw_cpu_reset(void)
{
    while (1);
}

/** shutdown CPU */
rt_weak void rt_hw_cpu_shutdown(void)
{
    rt_base_t level;

    rt_kprintf("shutdown...\n");

    level = rt_hw_interrupt_disable();
    while (level)
    {
        RT_ASSERT(0);
    }
}
