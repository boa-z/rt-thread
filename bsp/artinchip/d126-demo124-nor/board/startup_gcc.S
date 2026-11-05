/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D12p (D126) M-mode startup.
 *
 * The vendor bootloader (PBP + SPL) initializes PSRAM and loads the FIT
 * image at 0x40000000.  This file establishes the C runtime, the CLIC
 * vector table and the T-Head E907F machine state.
 */

#include <rtconfig.h>

    .section .vectors, "a", @progbits
    .align 6
    .globl __Vectors
    .type __Vectors, @object
__Vectors:
    .rept 3
    .long Default_IRQHandler
    .endr
    .long PendSV_Handler
    .rept 124
    .long Default_IRQHandler
    .endr
    .size __Vectors, . - __Vectors

    .section .text.start, "ax", @progbits
    .align 2
    .globl _start
    .globl Reset_Handler
    .type _start, @function
    .type Reset_Handler, @function
_start:
Reset_Handler:
    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop

    /* CLIC mode: exceptions go to the mtvec base, interrupts use mtvt. */
    la      t0, Default_Handler
    ori     t0, t0, 3
    csrw    mtvec, t0
    la      t0, __Vectors
    csrw    0x307, t0               /* mtvt */

    la      sp, __rt_rvstack

    /* Enable the single-precision FPU (FS = dirty). */
    li      t0, (3 << 13)
    csrs    mstatus, t0

    /*
     * MXSTATUS: enable unaligned accesses (MM, bit 15) and the T-Head
     * ISA extension (THEADISAEE, bit 22).  These bits are set by the
     * vendor SystemInit as well.
     */
    li      t0, (1 << 15) | (1 << 22)
    csrs    0x7c0, t0

    /* Invalidate and enable the I/D caches. */
    .long   0x0100000b              /* icache.iall */
    .long   0x0020000b              /* dcache.iall */
    fence
    fence.i
    csrr    t0, 0x7c1               /* MHCR */
    li      t1, 0x0000103f          /* IE|DE|WB|WA|RS|BPE|L0BTB */
    or      t0, t0, t1
    csrw    0x7c1, t0
    fence
    fence.i

    /* Clear BSS. */
    la      t0, __bss_start__
    la      t1, __bss_end__
1:
    bgeu    t0, t1, 2f
    sw      zero, 0(t0)
    addi    t0, t0, 4
    j       1b
2:
    la      a5, entry
    jalr    a5

3:
    j       3b
    .size _start, . - _start
    .size Reset_Handler, . - Reset_Handler
