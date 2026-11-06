/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * FPU context and FCSR regression tests.
 *
 * aic_fpu_context_test() sets a per-thread marker in the callee-saved
 * floating-point registers, selects a distinct dynamic rounding mode, runs
 * an operation whose result depends on that mode, sets distinct exception
 * flags, and then blocks in sync().  The values must survive a real thread
 * switch, so the checks run after sync() returns.
 *
 * aic_fpu_irq_test() does the same, but busy-waits through delay() instead
 * of blocking.  With two same-priority threads and a short time slice, the
 * timer interrupt must preempt an FPU-live thread and switch to its peer.
 *
 * Return codes:
 *   0        pass
 *   1..12    callee-saved fs0..fs11 mismatch
 *   13..17   context test: pre-switch result, post-switch result, NX flag,
 *            rounding mode, exception flags
 *   1..8     IRQ test: fs0..fs3, rounding mode, exception flags, result, NX
 */

    .text
    .align 2
    .globl aic_fpu_context_test
    .type aic_fpu_context_test, @function
aic_fpu_context_test:
    addi    sp, sp, -176
    sd      ra, 0(sp)
    sd      s0, 8(sp)
    sd      s1, 16(sp)
    sd      s2, 24(sp)
    sd      s3, 32(sp)
    sd      s4, 40(sp)
    csrr    t0, 0x003
    sd      t0, 48(sp)
    fsd     fs0, 56(sp)
    fsd     fs1, 64(sp)
    fsd     fs2, 72(sp)
    fsd     fs3, 80(sp)
    fsd     fs4, 88(sp)
    fsd     fs5, 96(sp)
    fsd     fs6, 104(sp)
    fsd     fs7, 112(sp)
    fsd     fs8, 120(sp)
    fsd     fs9, 128(sp)
    fsd     fs10, 136(sp)
    fsd     fs11, 144(sp)

    /* a0 = id, a1 = seed, a2 = sync, a3 = callback argument */
    mv      s3, a0
    mv      s0, a1
    mv      s2, a2
    mv      s1, a3

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

    /*
     * Select RUP for worker 0 and RTZ for worker 1.  DZ and OF are the
     * distinct sticky exception flags that must survive the switch.
     */
    andi    t0, s3, 1
    bnez    t0, .Lctx_mode_rtz
    li      t1, 3                   /* RUP */
    li      t2, 0x8                 /* DZ */
    j       .Lctx_mode_ready
.Lctx_mode_rtz:
    li      t1, 1                   /* RTZ */
    li      t2, 0x4                 /* OF */
.Lctx_mode_ready:
    slli    t3, t1, 5
    csrw    0x003, t3               /* frm = mode, fflags = 0 */

    /*
     * 1.0 + 2^-53 is exactly halfway between 1.0 and the next double.
     * RUP must round up; RTZ must keep 1.0.  The operation also sets NX.
     */
    li      t0, 0x3ff0000000000000
    fmv.d.x ft0, t0
    li      t0, 0x3ca0000000000000
    fmv.d.x ft1, t0
    fadd.d  ft2, ft0, ft1
    fmv.x.d s4, ft2

    or      t3, t3, t2
    csrw    0x003, t3               /* frm plus the per-worker flags */

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

    /* The pre-switch arithmetic result must have used this thread's frm. */
    andi    t0, s3, 1
    bnez    t0, .Lctx_pre_rtz
    li      t1, 0x3ff0000000000001
    j       .Lctx_pre_ready
.Lctx_pre_rtz:
    li      t1, 0x3ff0000000000000
.Lctx_pre_ready:
    bne     s4, t1, .Lfail13

    /* frm and fflags must come back exactly as this thread left them. */
    csrr    t0, 0x003
    andi    t1, t0, 0xE0
    andi    t2, s3, 1
    bnez    t2, .Lctx_chk_rtz
    li      t2, 3
    j       .Lctx_chk_mode
.Lctx_chk_rtz:
    li      t2, 1
.Lctx_chk_mode:
    slli    t2, t2, 5
    bne     t1, t2, .Lfail16

    andi    t1, t0, 0x1F
    andi    t2, s3, 1
    bnez    t2, .Lctx_chk_of
    li      t2, 0x8
    j       .Lctx_chk_flags
.Lctx_chk_of:
    li      t2, 0x4
.Lctx_chk_flags:
    bne     t1, t2, .Lfail17

    /* Re-run the tie after the switch and prove frm is still effective. */
    li      t0, 0x3ff0000000000000
    fmv.d.x ft0, t0
    li      t0, 0x3ca0000000000000
    fmv.d.x ft1, t0
    fadd.d  ft2, ft0, ft1
    fmv.x.d t1, ft2
    andi    t2, s3, 1
    bnez    t2, .Lctx_re_rtz
    li      t2, 0x3ff0000000000001
    j       .Lctx_re_ready
.Lctx_re_rtz:
    li      t2, 0x3ff0000000000000
.Lctx_re_ready:
    bne     t1, t2, .Lfail14

    csrr    t0, 0x003
    andi    t0, t0, 0x1
    beqz    t0, .Lfail15

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
    j       .Ldone
.Lfail13:
    li      a0, 13
    j       .Ldone
.Lfail14:
    li      a0, 14
    j       .Ldone
.Lfail15:
    li      a0, 15
    j       .Ldone
.Lfail16:
    li      a0, 16
    j       .Ldone
.Lfail17:
    li      a0, 17

.Ldone:
    ld      t0, 48(sp)
    csrw    0x003, t0
    ld      ra, 0(sp)
    ld      s0, 8(sp)
    ld      s1, 16(sp)
    ld      s2, 24(sp)
    ld      s3, 32(sp)
    ld      s4, 40(sp)
    fld     fs0, 56(sp)
    fld     fs1, 64(sp)
    fld     fs2, 72(sp)
    fld     fs3, 80(sp)
    fld     fs4, 88(sp)
    fld     fs5, 96(sp)
    fld     fs6, 104(sp)
    fld     fs7, 112(sp)
    fld     fs8, 120(sp)
    fld     fs9, 128(sp)
    fld     fs10, 136(sp)
    fld     fs11, 144(sp)
    addi    sp, sp, 176
    ret
    .size aic_fpu_context_test, . - aic_fpu_context_test

    .align 2
    .globl aic_fpu_irq_test
    .type aic_fpu_irq_test, @function
aic_fpu_irq_test:
    addi    sp, sp, -112
    sd      ra, 0(sp)
    sd      s0, 8(sp)
    sd      s1, 16(sp)
    sd      s2, 24(sp)
    sd      s3, 32(sp)
    csrr    t0, 0x003
    sd      t0, 40(sp)
    fsd     fs0, 48(sp)
    fsd     fs1, 56(sp)
    fsd     fs2, 64(sp)
    fsd     fs3, 72(sp)

    /* a0 = id, a1 = seed, a2 = delay, a3 = ticks */
    mv      s3, a0
    mv      s0, a1
    mv      s2, a2
    mv      s1, a3

    li      t0, 0x1100
    add     t0, s0, t0
    fmv.d.x fs0, t0
    li      t0, 0x2200
    add     t0, s0, t0
    fmv.d.x fs1, t0
    li      t0, 0x3300
    add     t0, s0, t0
    fmv.d.x fs2, t0
    li      t0, 0x4400
    add     t0, s0, t0
    fmv.d.x fs3, t0

    andi    t0, s3, 1
    bnez    t0, .Lirq_mode_rtz
    li      t1, 3                   /* RUP */
    li      t2, 0x8                 /* DZ */
    j       .Lirq_mode_ready
.Lirq_mode_rtz:
    li      t1, 1                   /* RTZ */
    li      t2, 0x4                 /* OF */
.Lirq_mode_ready:
    slli    t3, t1, 5
    or      t3, t3, t2
    csrw    0x003, t3

    /* Busy wait: timer interrupts must preempt this FPU-live thread. */
    mv      a0, s1
    jalr    s2

    li      t0, 0x1100
    add     t0, s0, t0
    fmv.x.d t1, fs0
    bne     t1, t0, .Lifail1
    li      t0, 0x2200
    add     t0, s0, t0
    fmv.x.d t1, fs1
    bne     t1, t0, .Lifail2
    li      t0, 0x3300
    add     t0, s0, t0
    fmv.x.d t1, fs2
    bne     t1, t0, .Lifail3
    li      t0, 0x4400
    add     t0, s0, t0
    fmv.x.d t1, fs3
    bne     t1, t0, .Lifail4

    csrr    t0, 0x003
    andi    t1, t0, 0xE0
    andi    t2, s3, 1
    bnez    t2, .Lirq_chk_rtz
    li      t2, 3
    j       .Lirq_chk_mode
.Lirq_chk_rtz:
    li      t2, 1
.Lirq_chk_mode:
    slli    t2, t2, 5
    bne     t1, t2, .Lifail5

    andi    t1, t0, 0x1F
    andi    t2, s3, 1
    bnez    t2, .Lirq_chk_of
    li      t2, 0x8
    j       .Lirq_chk_flags
.Lirq_chk_of:
    li      t2, 0x4
.Lirq_chk_flags:
    bne     t1, t2, .Lifail6

    li      t0, 0x3ff0000000000000
    fmv.d.x ft0, t0
    li      t0, 0x3ca0000000000000
    fmv.d.x ft1, t0
    fadd.d  ft2, ft0, ft1
    fmv.x.d t1, ft2
    andi    t2, s3, 1
    bnez    t2, .Lirq_re_rtz
    li      t2, 0x3ff0000000000001
    j       .Lirq_re_ready
.Lirq_re_rtz:
    li      t2, 0x3ff0000000000000
.Lirq_re_ready:
    bne     t1, t2, .Lifail7

    csrr    t0, 0x003
    andi    t0, t0, 0x1
    beqz    t0, .Lifail8

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

.Lidone:
    ld      t0, 40(sp)
    csrw    0x003, t0
    ld      ra, 0(sp)
    ld      s0, 8(sp)
    ld      s1, 16(sp)
    ld      s2, 24(sp)
    ld      s3, 32(sp)
    fld     fs0, 48(sp)
    fld     fs1, 56(sp)
    fld     fs2, 64(sp)
    fld     fs3, 72(sp)
    addi    sp, sp, 112
    ret
    .size aic_fpu_irq_test, . - aic_fpu_irq_test
