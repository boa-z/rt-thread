/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * T-Head E9xx RV32 context switch and interrupt mask helpers.
 *
 * The E9xx CLIC software interrupt (vector 3) is used as the context
 * switch trap.  The pending bit lives in the T-Head TSPEND register.
 */

#include "cpuport.h"

#define TSPEND_INTIP AIC_TSPEND_ADDR

    .section .text.entry, "ax"
    .align 2
    .globl rt_hw_context_switch_to
    .type rt_hw_context_switch_to, @function
rt_hw_context_switch_to:
    la      t0, rt_interrupt_to_thread
    sw      a0, 0(t0)
    la      t0, rt_interrupt_from_thread
    sw      zero, 0(t0)
    la      t0, rt_thread_switch_interrupt_flag
    li      t1, 1
    sw      t1, 0(t0)

    li      t0, TSPEND_INTIP
    lb      t1, 0(t0)
    ori     t1, t1, 1
    sb      t1, 0(t0)
    fence
    lb      t1, 0(t0)
    fence
    AIC_SYNC_I

    csrsi   mstatus, 8
    ret
    .size rt_hw_context_switch_to, . - rt_hw_context_switch_to

/*
 * void rt_hw_context_switch(rt_ubase_t from, rt_ubase_t to);
 *
 * Called with interrupts disabled.  The actual register save/restore is
 * performed by PendSV_Handler after the caller re-enables interrupts.
 */
    .globl rt_hw_context_switch
    .globl rt_hw_context_switch_interrupt
    .type rt_hw_context_switch, @function
    .type rt_hw_context_switch_interrupt, @function
rt_hw_context_switch:
rt_hw_context_switch_interrupt:
    addi    sp, sp, -(3 * REGBYTES)
    sw      t0, 0 * REGBYTES(sp)
    sw      t1, 1 * REGBYTES(sp)
    sw      t2, 2 * REGBYTES(sp)

    la      t0, rt_thread_switch_interrupt_flag
    lw      t1, 0(t0)
    li      t2, 1
    beq     t1, t2, .Lreswitch

    sw      t2, 0(t0)
    la      t0, rt_interrupt_from_thread
    sw      a0, 0(t0)

.Lreswitch:
    la      t0, rt_interrupt_to_thread
    sw      a1, 0(t0)

    li      t0, TSPEND_INTIP
    lb      t1, 0(t0)
    ori     t1, t1, 1
    sb      t1, 0(t0)
    fence
    lb      t1, 0(t0)
    fence
    AIC_SYNC_I

    lw      t0, 0 * REGBYTES(sp)
    lw      t1, 1 * REGBYTES(sp)
    lw      t2, 2 * REGBYTES(sp)
    addi    sp, sp, (3 * REGBYTES)
    ret
    .size rt_hw_context_switch, . - rt_hw_context_switch
    .size rt_hw_context_switch_interrupt, . - rt_hw_context_switch_interrupt

/*
 * CLIC vector 3.  Hardware has already saved mepc/mstatus and disabled
 * interrupts.  The full integer and FPU context is stored on the current
 * thread stack.  gp is fixed and is intentionally not restored.
 */
    .globl PendSV_Handler
    .type PendSV_Handler, @function
PendSV_Handler:
    addi    sp, sp, -(64 * REGBYTES)

    sw      x1, 1 * REGBYTES(sp)
    csrr    t0, mepc
    sw      t0, 0 * REGBYTES(sp)
    csrr    t0, mstatus
    sw      t0, 2 * REGBYTES(sp)
    sw      x4, 4 * REGBYTES(sp)
    sw      x5, 5 * REGBYTES(sp)
    sw      x6, 6 * REGBYTES(sp)
    sw      x7, 7 * REGBYTES(sp)
    sw      x8, 8 * REGBYTES(sp)
    sw      x9, 9 * REGBYTES(sp)
    sw      x10, 10 * REGBYTES(sp)
    sw      x11, 11 * REGBYTES(sp)
    sw      x12, 12 * REGBYTES(sp)
    sw      x13, 13 * REGBYTES(sp)
    sw      x14, 14 * REGBYTES(sp)
    sw      x15, 15 * REGBYTES(sp)
    sw      x16, 16 * REGBYTES(sp)
    sw      x17, 17 * REGBYTES(sp)
    sw      x18, 18 * REGBYTES(sp)
    sw      x19, 19 * REGBYTES(sp)
    sw      x20, 20 * REGBYTES(sp)
    sw      x21, 21 * REGBYTES(sp)
    sw      x22, 22 * REGBYTES(sp)
    sw      x23, 23 * REGBYTES(sp)
    sw      x24, 24 * REGBYTES(sp)
    sw      x25, 25 * REGBYTES(sp)
    sw      x26, 26 * REGBYTES(sp)
    sw      x27, 27 * REGBYTES(sp)
    sw      x28, 28 * REGBYTES(sp)
    sw      x29, 29 * REGBYTES(sp)
    sw      x30, 30 * REGBYTES(sp)
    sw      x31, 31 * REGBYTES(sp)

#ifdef ARCH_RISCV_FPU
    fsw     f0, 32 * FREGBYTES(sp)
    fsw     f1, 33 * FREGBYTES(sp)
    fsw     f2, 34 * FREGBYTES(sp)
    fsw     f3, 35 * FREGBYTES(sp)
    fsw     f4, 36 * FREGBYTES(sp)
    fsw     f5, 37 * FREGBYTES(sp)
    fsw     f6, 38 * FREGBYTES(sp)
    fsw     f7, 39 * FREGBYTES(sp)
    fsw     f8, 40 * FREGBYTES(sp)
    fsw     f9, 41 * FREGBYTES(sp)
    fsw     f10, 42 * FREGBYTES(sp)
    fsw     f11, 43 * FREGBYTES(sp)
    fsw     f12, 44 * FREGBYTES(sp)
    fsw     f13, 45 * FREGBYTES(sp)
    fsw     f14, 46 * FREGBYTES(sp)
    fsw     f15, 47 * FREGBYTES(sp)
    fsw     f16, 48 * FREGBYTES(sp)
    fsw     f17, 49 * FREGBYTES(sp)
    fsw     f18, 50 * FREGBYTES(sp)
    fsw     f19, 51 * FREGBYTES(sp)
    fsw     f20, 52 * FREGBYTES(sp)
    fsw     f21, 53 * FREGBYTES(sp)
    fsw     f22, 54 * FREGBYTES(sp)
    fsw     f23, 55 * FREGBYTES(sp)
    fsw     f24, 56 * FREGBYTES(sp)
    fsw     f25, 57 * FREGBYTES(sp)
    fsw     f26, 58 * FREGBYTES(sp)
    fsw     f27, 59 * FREGBYTES(sp)
    fsw     f28, 60 * FREGBYTES(sp)
    fsw     f29, 61 * FREGBYTES(sp)
    fsw     f30, 62 * FREGBYTES(sp)
    fsw     f31, 63 * FREGBYTES(sp)
#endif

    la      t0, rt_thread_switch_interrupt_flag
    lw      t1, 0(t0)
    beqz    t1, .Lrestore

    sw      zero, 0(t0)
    la      t0, rt_interrupt_from_thread
    lw      t1, 0(t0)
    beqz    t1, .Lload_to

    sw      sp, 0(t1)

.Lload_to:
    la      t0, rt_interrupt_to_thread
    lw      t1, 0(t0)
    lw      sp, 0(t1)

.Lrestore:
    lw      t0, 0 * REGBYTES(sp)
    csrw    mepc, t0
    lw      t0, 2 * REGBYTES(sp)
    csrw    mstatus, t0

    lw      x1, 1 * REGBYTES(sp)
    lw      x4, 4 * REGBYTES(sp)
    lw      x5, 5 * REGBYTES(sp)
    lw      x6, 6 * REGBYTES(sp)
    lw      x7, 7 * REGBYTES(sp)
    lw      x8, 8 * REGBYTES(sp)
    lw      x9, 9 * REGBYTES(sp)
    lw      x10, 10 * REGBYTES(sp)
    lw      x11, 11 * REGBYTES(sp)
    lw      x12, 12 * REGBYTES(sp)
    lw      x13, 13 * REGBYTES(sp)
    lw      x14, 14 * REGBYTES(sp)
    lw      x15, 15 * REGBYTES(sp)
    lw      x16, 16 * REGBYTES(sp)
    lw      x17, 17 * REGBYTES(sp)
    lw      x18, 18 * REGBYTES(sp)
    lw      x19, 19 * REGBYTES(sp)
    lw      x20, 20 * REGBYTES(sp)
    lw      x21, 21 * REGBYTES(sp)
    lw      x22, 22 * REGBYTES(sp)
    lw      x23, 23 * REGBYTES(sp)
    lw      x24, 24 * REGBYTES(sp)
    lw      x25, 25 * REGBYTES(sp)
    lw      x26, 26 * REGBYTES(sp)
    lw      x27, 27 * REGBYTES(sp)
    lw      x28, 28 * REGBYTES(sp)
    lw      x29, 29 * REGBYTES(sp)
    lw      x30, 30 * REGBYTES(sp)
    lw      x31, 31 * REGBYTES(sp)

#ifdef ARCH_RISCV_FPU
    flw     f0, 32 * FREGBYTES(sp)
    flw     f1, 33 * FREGBYTES(sp)
    flw     f2, 34 * FREGBYTES(sp)
    flw     f3, 35 * FREGBYTES(sp)
    flw     f4, 36 * FREGBYTES(sp)
    flw     f5, 37 * FREGBYTES(sp)
    flw     f6, 38 * FREGBYTES(sp)
    flw     f7, 39 * FREGBYTES(sp)
    flw     f8, 40 * FREGBYTES(sp)
    flw     f9, 41 * FREGBYTES(sp)
    flw     f10, 42 * FREGBYTES(sp)
    flw     f11, 43 * FREGBYTES(sp)
    flw     f12, 44 * FREGBYTES(sp)
    flw     f13, 45 * FREGBYTES(sp)
    flw     f14, 46 * FREGBYTES(sp)
    flw     f15, 47 * FREGBYTES(sp)
    flw     f16, 48 * FREGBYTES(sp)
    flw     f17, 49 * FREGBYTES(sp)
    flw     f18, 50 * FREGBYTES(sp)
    flw     f19, 51 * FREGBYTES(sp)
    flw     f20, 52 * FREGBYTES(sp)
    flw     f21, 53 * FREGBYTES(sp)
    flw     f22, 54 * FREGBYTES(sp)
    flw     f23, 55 * FREGBYTES(sp)
    flw     f24, 56 * FREGBYTES(sp)
    flw     f25, 57 * FREGBYTES(sp)
    flw     f26, 58 * FREGBYTES(sp)
    flw     f27, 59 * FREGBYTES(sp)
    flw     f28, 60 * FREGBYTES(sp)
    flw     f29, 61 * FREGBYTES(sp)
    flw     f30, 62 * FREGBYTES(sp)
    flw     f31, 63 * FREGBYTES(sp)
#endif

    addi    sp, sp, (64 * REGBYTES)
    mret
    .size PendSV_Handler, . - PendSV_Handler

    .globl rt_hw_interrupt_disable
    .type rt_hw_interrupt_disable, @function
rt_hw_interrupt_disable:
    csrrci  a0, mstatus, 8
    ret
    .size rt_hw_interrupt_disable, . - rt_hw_interrupt_disable

    .globl rt_hw_interrupt_enable
    .type rt_hw_interrupt_enable, @function
rt_hw_interrupt_enable:
    csrw    mstatus, a0
    ret
    .size rt_hw_interrupt_enable, . - rt_hw_interrupt_enable
