/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Integer callee-saved register context test.
 *
 * The C caller loads a per-thread marker into s0..s11, then calls
 * sync(), which must block and force a real thread switch.  On return
 * the marker is checked.  A failure is reported as the 1-based index
 * of the first mismatching integer register.
 */

    .text
    .align 2
    .globl aic_integer_context_test
    .type aic_integer_context_test, @function
aic_integer_context_test:
    addi    sp, sp, -128
    sd      ra, 0(sp)
    sd      s0, 8(sp)
    sd      s1, 16(sp)
    sd      s2, 24(sp)
    sd      s3, 32(sp)
    sd      s4, 40(sp)
    sd      s5, 48(sp)
    sd      s6, 56(sp)
    sd      s7, 64(sp)
    sd      s8, 72(sp)
    sd      s9, 80(sp)
    sd      s10, 88(sp)
    sd      s11, 96(sp)
    sd      a1, 104(sp)
    sd      a2, 112(sp)
    sd      a0, 120(sp)

    /* a0 = seed, a1 = sync callback, a2 = callback argument */
    li      t0, 0x100
    add     s0, a0, t0
    li      t0, 0x200
    add     s1, a0, t0
    li      t0, 0x300
    add     s2, a0, t0
    li      t0, 0x400
    add     s3, a0, t0
    li      t0, 0x500
    add     s4, a0, t0
    li      t0, 0x600
    add     s5, a0, t0
    li      t0, 0x700
    add     s6, a0, t0
    li      t0, 0x800
    add     s7, a0, t0
    li      t0, 0x900
    add     s8, a0, t0
    li      t0, 0xA00
    add     s9, a0, t0
    li      t0, 0xB00
    add     s10, a0, t0
    li      t0, 0xC00
    add     s11, a0, t0

    /* This call must block and force a real context switch. */
    ld      a0, 112(sp)
    ld      t1, 104(sp)
    jalr    t1

    ld      t2, 120(sp)

    li      t0, 0x100
    add     t0, t2, t0
    bne     s0, t0, .Lifail1
    li      t0, 0x200
    add     t0, t2, t0
    bne     s1, t0, .Lifail2
    li      t0, 0x300
    add     t0, t2, t0
    bne     s2, t0, .Lifail3
    li      t0, 0x400
    add     t0, t2, t0
    bne     s3, t0, .Lifail4
    li      t0, 0x500
    add     t0, t2, t0
    bne     s4, t0, .Lifail5
    li      t0, 0x600
    add     t0, t2, t0
    bne     s5, t0, .Lifail6
    li      t0, 0x700
    add     t0, t2, t0
    bne     s6, t0, .Lifail7
    li      t0, 0x800
    add     t0, t2, t0
    bne     s7, t0, .Lifail8
    li      t0, 0x900
    add     t0, t2, t0
    bne     s8, t0, .Lifail9
    li      t0, 0xA00
    add     t0, t2, t0
    bne     s9, t0, .Lifail10
    li      t0, 0xB00
    add     t0, t2, t0
    bne     s10, t0, .Lifail11
    li      t0, 0xC00
    add     t0, t2, t0
    bne     s11, t0, .Lifail12

    li      a0, 0
    j       .Lidone

.Lifail1:
    li      a0, 1
    j       .Lidone
.Lifail2:
    li      a0, 2
    j       .Lidone
.Lifail3:
    li      a0, 3
    j       .Lidone
.Lifail4:
    li      a0, 4
    j       .Lidone
.Lifail5:
    li      a0, 5
    j       .Lidone
.Lifail6:
    li      a0, 6
    j       .Lidone
.Lifail7:
    li      a0, 7
    j       .Lidone
.Lifail8:
    li      a0, 8
    j       .Lidone
.Lifail9:
    li      a0, 9
    j       .Lidone
.Lifail10:
    li      a0, 10
    j       .Lidone
.Lifail11:
    li      a0, 11
    j       .Lidone
.Lifail12:
    li      a0, 12

.Lidone:
    ld      ra, 0(sp)
    ld      s0, 8(sp)
    ld      s1, 16(sp)
    ld      s2, 24(sp)
    ld      s3, 32(sp)
    ld      s4, 40(sp)
    ld      s5, 48(sp)
    ld      s6, 56(sp)
    ld      s7, 64(sp)
    ld      s8, 72(sp)
    ld      s9, 80(sp)
    ld      s10, 88(sp)
    ld      s11, 96(sp)
    addi    sp, sp, 128
    ret
    .size aic_integer_context_test, . - aic_integer_context_test
