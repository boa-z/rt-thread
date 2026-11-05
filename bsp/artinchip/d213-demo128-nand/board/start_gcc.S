/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal M-mode startup for the ArtInChip D21x C906.
 *
 * The ArtInChip bootloader (PBP + loader) loads the FIT image at
 * 0x40000000 and jumps to its entry point.  RT-Thread's generic RISC-V
 * M-mode port owns the trap entry, context switch and interrupt stack;
 * this file only establishes the C runtime environment.
 */

#include <rtconfig.h>

    .section .text.start, "ax"
    .align 2
    .globl _start
    .type _start, @function
_start:
    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop

    /* Disable all interrupts until the scheduler starts. */
    csrw    mie, zero
    csrw    mip, zero

    /* Machine trap vector: generic RT-Thread RISC-V trap entry. */
    la      t0, trap_entry
    csrw    mtvec, t0

    /* Boot stack; also used as the interrupt stack top. */
    la      sp, __rt_rvstack
    csrw    mscratch, zero

    /* Enable the FPU (FS = Initial). */
    li      t0, (1 << 13)
    csrs    mstatus, t0

    /* Clear BSS. */
    la      t0, __bss_start__
    la      t1, __bss_end__
1:
    bgeu    t0, t1, 2f
    sd      zero, 0(t0)
    addi    t0, t0, 8
    j       1b
2:
    call    entry

3:
    j       3b
    .size _start, . - _start
