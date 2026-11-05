/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * FPU context-switch test.
 *
 * The C caller sets a per-thread marker into the callee-saved floating
 * point registers, then calls sync() which must block and let another
 * thread run.  On return the marker is checked.  A failure is reported
 * as the 1-based index of the first mismatching callee-saved register.
 */

    .text
    .align 2
    .globl aic_fpu_context_test
    .type aic_fpu_context_test, @function
aic_fpu_context_test:
    addi    sp, sp, -160
    sd      ra, 0(sp)
    sd      s0, 8(sp)
    sd      s1, 16(sp)
    sd      s2, 24(sp)
    fsd     fs0, 32(sp)
    fsd     fs1, 40(sp)
    fsd     fs2, 48(sp)
    fsd     fs3, 56(sp)
    fsd     fs4, 64(sp)
    fsd     fs5, 72(sp)
    fsd     fs6, 80(sp)
    fsd     fs7, 88(sp)
    fsd     fs8, 96(sp)
    fsd     fs9, 104(sp)
    fsd     fs10, 112(sp)
    fsd     fs11, 120(sp)

    /* a0 = seed, a1 = sync callback, a2 = callback argument */
    mv      s0, a0
    mv      s2, a1
    mv      s1, a2

    li      t0, 0x100
    add     t0, s0, t0
    fmv.d.x fs0, t0
    li      t0, 0x200
    add     t0, s0, t0
    fmv.d.x fs1, t0
    li      t0, 0x300
    add     t0, s0, t0
    fmv.d.x fs2, t0
    li      t0, 0x400
    add     t0, s0, t0
    fmv.d.x fs3, t0
    li      t0, 0x500
    add     t0, s0, t0
    fmv.d.x fs4, t0
    li      t0, 0x600
    add     t0, s0, t0
    fmv.d.x fs5, t0
    li      t0, 0x700
    add     t0, s0, t0
    fmv.d.x fs6, t0
    li      t0, 0x800
    add     t0, s0, t0
    fmv.d.x fs7, t0
    li      t0, 0x900
    add     t0, s0, t0
    fmv.d.x fs8, t0
    li      t0, 0xA00
    add     t0, s0, t0
    fmv.d.x fs9, t0
    li      t0, 0xB00
    add     t0, s0, t0
    fmv.d.x fs10, t0
    li      t0, 0xC00
    add     t0, s0, t0
    fmv.d.x fs11, t0

    /* This call must block and force a real context switch. */
    mv      a0, s1
    jalr    s2

    li      t0, 0x100
    add     t0, s0, t0
    fmv.x.d t1, fs0
    bne     t1, t0, .Lfail1
    li      t0, 0x200
    add     t0, s0, t0
    fmv.x.d t1, fs1
    bne     t1, t0, .Lfail2
    li      t0, 0x300
    add     t0, s0, t0
    fmv.x.d t1, fs2
    bne     t1, t0, .Lfail3
    li      t0, 0x400
    add     t0, s0, t0
    fmv.x.d t1, fs3
    bne     t1, t0, .Lfail4
    li      t0, 0x500
    add     t0, s0, t0
    fmv.x.d t1, fs4
    bne     t1, t0, .Lfail5
    li      t0, 0x600
    add     t0, s0, t0
    fmv.x.d t1, fs5
    bne     t1, t0, .Lfail6
    li      t0, 0x700
    add     t0, s0, t0
    fmv.x.d t1, fs6
    bne     t1, t0, .Lfail7
    li      t0, 0x800
    add     t0, s0, t0
    fmv.x.d t1, fs7
    bne     t1, t0, .Lfail8
    li      t0, 0x900
    add     t0, s0, t0
    fmv.x.d t1, fs8
    bne     t1, t0, .Lfail9
    li      t0, 0xA00
    add     t0, s0, t0
    fmv.x.d t1, fs9
    bne     t1, t0, .Lfail10
    li      t0, 0xB00
    add     t0, s0, t0
    fmv.x.d t1, fs10
    bne     t1, t0, .Lfail11
    li      t0, 0xC00
    add     t0, s0, t0
    fmv.x.d t1, fs11
    bne     t1, t0, .Lfail12

    li      a0, 0
    j       .Ldone

.Lfail1:
    li      a0, 1
    j       .Ldone
.Lfail2:
    li      a0, 2
    j       .Ldone
.Lfail3:
    li      a0, 3
    j       .Ldone
.Lfail4:
    li      a0, 4
    j       .Ldone
.Lfail5:
    li      a0, 5
    j       .Ldone
.Lfail6:
    li      a0, 6
    j       .Ldone
.Lfail7:
    li      a0, 7
    j       .Ldone
.Lfail8:
    li      a0, 8
    j       .Ldone
.Lfail9:
    li      a0, 9
    j       .Ldone
.Lfail10:
    li      a0, 10
    j       .Ldone
.Lfail11:
    li      a0, 11
    j       .Ldone
.Lfail12:
    li      a0, 12

.Ldone:
    ld      ra, 0(sp)
    ld      s0, 8(sp)
    ld      s1, 16(sp)
    ld      s2, 24(sp)
    fld     fs0, 32(sp)
    fld     fs1, 40(sp)
    fld     fs2, 48(sp)
    fld     fs3, 56(sp)
    fld     fs4, 64(sp)
    fld     fs5, 72(sp)
    fld     fs6, 80(sp)
    fld     fs7, 88(sp)
    fld     fs8, 96(sp)
    fld     fs9, 104(sp)
    fld     fs10, 112(sp)
    fld     fs11, 120(sp)
    addi    sp, sp, 160
    ret
    .size aic_fpu_context_test, . - aic_fpu_context_test
