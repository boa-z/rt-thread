/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * D12p CLIC interrupt and exception entry.
 *
 * CLIC vector mode dispatches external interrupts directly through the
 * mtvt table.  Entry 3 is reserved for the software interrupt used by
 * the context switch.  All other entries land here, save the caller
 * saved state and call the C dispatcher.
 */

#include <rtconfig.h>

    .section .text.entry, "ax"
    .align 2
    .globl Default_IRQHandler
    .type Default_IRQHandler, @function
Default_IRQHandler:
    addi    sp, sp, -160

    sw      ra, 0(sp)
    sw      t0, 4(sp)
    sw      t1, 8(sp)
    sw      t2, 12(sp)
    sw      t3, 16(sp)
    sw      t4, 20(sp)
    sw      t5, 24(sp)
    sw      t6, 28(sp)
    sw      a0, 32(sp)
    sw      a1, 36(sp)
    sw      a2, 40(sp)
    sw      a3, 44(sp)
    sw      a4, 48(sp)
    sw      a5, 52(sp)
    sw      a6, 56(sp)
    sw      a7, 60(sp)

#if defined(__riscv_flen) && (__riscv_flen == 32)
    fsw     ft0, 80(sp)
    fsw     ft1, 84(sp)
    fsw     ft2, 88(sp)
    fsw     ft3, 92(sp)
    fsw     ft4, 96(sp)
    fsw     ft5, 100(sp)
    fsw     ft6, 104(sp)
    fsw     ft7, 108(sp)
    fsw     fa0, 112(sp)
    fsw     fa1, 116(sp)
    fsw     fa2, 120(sp)
    fsw     fa3, 124(sp)
    fsw     fa4, 128(sp)
    fsw     fa5, 132(sp)
    fsw     fa6, 136(sp)
    fsw     fa7, 140(sp)
    fsw     ft8, 144(sp)
    fsw     ft9, 148(sp)
    fsw     ft10, 152(sp)
    fsw     ft11, 156(sp)
#endif

    call    rt_interrupt_enter

    csrr    a0, mcause
    andi    a0, a0, 0x3ff
    call    aic_irq_dispatch

    call    rt_interrupt_leave

#if defined(__riscv_flen) && (__riscv_flen == 32)
    flw     ft0, 80(sp)
    flw     ft1, 84(sp)
    flw     ft2, 88(sp)
    flw     ft3, 92(sp)
    flw     ft4, 96(sp)
    flw     ft5, 100(sp)
    flw     ft6, 104(sp)
    flw     ft7, 108(sp)
    flw     fa0, 112(sp)
    flw     fa1, 116(sp)
    flw     fa2, 120(sp)
    flw     fa3, 124(sp)
    flw     fa4, 128(sp)
    flw     fa5, 132(sp)
    flw     fa6, 136(sp)
    flw     fa7, 140(sp)
    flw     ft8, 144(sp)
    flw     ft9, 148(sp)
    flw     ft10, 152(sp)
    flw     ft11, 156(sp)
#endif

    lw      ra, 0(sp)
    lw      t0, 4(sp)
    lw      t1, 8(sp)
    lw      t2, 12(sp)
    lw      t3, 16(sp)
    lw      t4, 20(sp)
    lw      t5, 24(sp)
    lw      t6, 28(sp)
    lw      a0, 32(sp)
    lw      a1, 36(sp)
    lw      a2, 40(sp)
    lw      a3, 44(sp)
    lw      a4, 48(sp)
    lw      a5, 52(sp)
    lw      a6, 56(sp)
    lw      a7, 60(sp)

    addi    sp, sp, 160
    mret
    .size Default_IRQHandler, . - Default_IRQHandler

/*
 * Exception entry (mtvec base).  NMI also lands here on this core; it
 * is reported as an exception instead of being silently discarded.
 */
    .section .text.trap, "ax"
    .align 6
    .globl Default_Handler
    .type Default_Handler, @function
Default_Handler:
    addi    sp, sp, -128

    sw      ra, 0(sp)
    sw      t0, 4(sp)
    sw      t1, 8(sp)
    sw      t2, 12(sp)
    sw      t3, 16(sp)
    sw      t4, 20(sp)
    sw      t5, 24(sp)
    sw      t6, 28(sp)
    sw      a0, 32(sp)
    sw      a1, 36(sp)
    sw      a2, 40(sp)
    sw      a3, 44(sp)
    sw      a4, 48(sp)
    sw      a5, 52(sp)
    sw      a6, 56(sp)
    sw      a7, 60(sp)
    sw      s0, 64(sp)
    sw      s1, 68(sp)
    sw      s2, 72(sp)
    sw      s3, 76(sp)
    sw      s4, 80(sp)
    sw      s5, 84(sp)
    sw      s6, 88(sp)
    sw      s7, 92(sp)
    sw      s8, 96(sp)
    sw      s9, 100(sp)
    sw      s10, 104(sp)
    sw      s11, 108(sp)
    sw      gp, 112(sp)
    sw      tp, 116(sp)

    csrr    a0, mcause
    csrr    a1, mepc
    csrr    a2, mtval
    mv      a3, sp
    call    aic_exception_handler

1:
    j       1b
    .size Default_Handler, . - Default_Handler
