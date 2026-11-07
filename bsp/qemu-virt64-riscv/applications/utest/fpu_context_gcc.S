/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Independent RISC-V FPU context probes for the QEMU virt64 BSP.
 *
 * These routines deliberately use assembly so that the compiler cannot
 * reorder, spill or optimise away the floating-point register accesses.
 *
 * ABI rules exercised here:
 *   - fs0-fs11 (f8, f9, f18-f27) are callee-saved and must survive any
 *     function call, including a call that blocks and reschedules.
 *   - f0-f31 and fcsr are caller-saved across calls, but a *trap* must
 *     preserve the interrupted instruction stream's complete FPU state.
 *
 * Only RV64D is supported by these probes.
 */

#include <rtconfig.h>
#include <csr.h>

#if __riscv_xlen != 64
#error "fpu_context_gcc.S requires RV64"
#endif

#if __riscv_flen != 64
#error "fpu_context_gcc.S requires the D extension"
#endif

#ifndef ARCH_RISCV_FPU
#error "fpu_context_gcc.S requires ARCH_RISCV_FPU"
#endif

    .text
    .align 2

/*
 * int fpu_fs_yield_probe(unsigned id, unsigned rounds,
 *                        void (*yield_fn)(void *), void *arg,
 *                        struct fpu_fail *out)
 *
 * Set fs0-fs11 to a per-thread/per-round pattern, call yield_fn(arg),
 * and verify the pattern after the call.  fs0-fs11 are callee-saved, so
 * any function call (including one that blocks) must preserve them.
 *
 * Return: 0 on success, 1..12 for the first mismatching fs register.
 */
    .globl fpu_fs_yield_probe
    .type fpu_fs_yield_probe, @function
fpu_fs_yield_probe:
    addi    sp, sp, -176
    sd      ra,   0(sp)
    sd      s0,   8(sp)
    sd      s1,  16(sp)
    sd      s2,  24(sp)
    sd      s3,  32(sp)
    sd      s4,  40(sp)
    sd      s5,  48(sp)
    sd      s6,  56(sp)
    csrr    t0, 0x003
    sd      t0,  64(sp)
    csrr    t0, CSR_STATUS
    sd      t0,  72(sp)
    fsd     fs0,  80(sp)
    fsd     fs1,  88(sp)
    fsd     fs2,  96(sp)
    fsd     fs3, 104(sp)
    fsd     fs4, 112(sp)
    fsd     fs5, 120(sp)
    fsd     fs6, 128(sp)
    fsd     fs7, 136(sp)
    fsd     fs8, 144(sp)
    fsd     fs9, 152(sp)
    fsd     fs10, 160(sp)
    fsd     fs11, 168(sp)

    mv      s0, a0              /* id */
    mv      s1, a1              /* rounds */
    mv      s2, a2              /* yield_fn */
    mv      s3, a3              /* arg */
    mv      s4, a4              /* out */

    /* Make sure the FPU is usable in this context. */
    li      t0, SR_FS
    csrs    CSR_STATUS, t0

    /* Per-thread base marker: 0x3ff0000000000000 + (id << 32). */
    li      s6, 0x3ff0000000000000
    slli    t0, s0, 32
    add     s6, s6, t0
    li      s5, 0               /* round */

.Lyield_loop:
    /* expected base for this round: base + (round << 16). */
    slli    t0, s5, 16
    add     t1, s6, t0

    /* fs0..fs11 = expected base + register index. */
    fmv.d.x fs0, t1
    addi    t2, t1, 1
    fmv.d.x fs1, t2
    addi    t2, t1, 2
    fmv.d.x fs2, t2
    addi    t2, t1, 3
    fmv.d.x fs3, t2
    addi    t2, t1, 4
    fmv.d.x fs4, t2
    addi    t2, t1, 5
    fmv.d.x fs5, t2
    addi    t2, t1, 6
    fmv.d.x fs6, t2
    addi    t2, t1, 7
    fmv.d.x fs7, t2
    addi    t2, t1, 8
    fmv.d.x fs8, t2
    addi    t2, t1, 9
    fmv.d.x fs9, t2
    addi    t2, t1, 10
    fmv.d.x fs10, t2
    addi    t2, t1, 11
    fmv.d.x fs11, t2

    /* Give the peer a distinct dynamic FP environment as well. */
    andi    t0, s0, 1
    bnez    t0, .Lfcsr_odd
    li      t0, 0x68            /* frm=RUP, fflags=DZ */
    j       .Lfcsr_set
.Lfcsr_odd:
    li      t0, 0x24            /* frm=RTZ, fflags=OF */
.Lfcsr_set:
    csrw    0x003, t0

    /* This call must preserve fs0-fs11 if the kernel is correct. */
    mv      a0, s3
    jalr    s2

    /* Recompute the expected base; t1 is caller-saved. */
    slli    t0, s5, 16
    add     t1, s6, t0

    fmv.x.d t2, fs0
    addi    t3, t1, 0
    beq     t2, t3, .Lfs_ok0
    li      t4, 1
    j       .Lyield_fail
.Lfs_ok0:
    fmv.x.d t2, fs1
    addi    t3, t1, 1
    beq     t2, t3, .Lfs_ok1
    li      t4, 2
    j       .Lyield_fail
.Lfs_ok1:
    fmv.x.d t2, fs2
    addi    t3, t1, 2
    beq     t2, t3, .Lfs_ok2
    li      t4, 3
    j       .Lyield_fail
.Lfs_ok2:
    fmv.x.d t2, fs3
    addi    t3, t1, 3
    beq     t2, t3, .Lfs_ok3
    li      t4, 4
    j       .Lyield_fail
.Lfs_ok3:
    fmv.x.d t2, fs4
    addi    t3, t1, 4
    beq     t2, t3, .Lfs_ok4
    li      t4, 5
    j       .Lyield_fail
.Lfs_ok4:
    fmv.x.d t2, fs5
    addi    t3, t1, 5
    beq     t2, t3, .Lfs_ok5
    li      t4, 6
    j       .Lyield_fail
.Lfs_ok5:
    fmv.x.d t2, fs6
    addi    t3, t1, 6
    beq     t2, t3, .Lfs_ok6
    li      t4, 7
    j       .Lyield_fail
.Lfs_ok6:
    fmv.x.d t2, fs7
    addi    t3, t1, 7
    beq     t2, t3, .Lfs_ok7
    li      t4, 8
    j       .Lyield_fail
.Lfs_ok7:
    fmv.x.d t2, fs8
    addi    t3, t1, 8
    beq     t2, t3, .Lfs_ok8
    li      t4, 9
    j       .Lyield_fail
.Lfs_ok8:
    fmv.x.d t2, fs9
    addi    t3, t1, 9
    beq     t2, t3, .Lfs_ok9
    li      t4, 10
    j       .Lyield_fail
.Lfs_ok9:
    fmv.x.d t2, fs10
    addi    t3, t1, 10
    beq     t2, t3, .Lfs_ok10
    li      t4, 11
    j       .Lyield_fail
.Lfs_ok10:
    fmv.x.d t2, fs11
    addi    t3, t1, 11
    beq     t2, t3, .Lfs_ok11
    li      t4, 12
    j       .Lyield_fail
.Lfs_ok11:
    addi    s5, s5, 1
    bltu    s5, s1, .Lyield_loop

    li      a0, 0
    j       .Lyield_epilogue

.Lyield_fail:
    beqz    s4, .Lyield_fail_ret
    sw      t4,   0(s4)         /* code */
    sw      s5,   4(s4)         /* round */
    addi    t5, t4, -1
    sw      t5,   8(s4)         /* register index */
    sd      t3,  16(s4)         /* expected */
    sd      t2,  24(s4)         /* actual */
.Lyield_fail_ret:
    mv      a0, t4

.Lyield_epilogue:
    ld      t0,  64(sp)
    csrw    0x003, t0
    li      t0, SR_FS
    csrs    CSR_STATUS, t0
    fld     fs0,  80(sp)
    fld     fs1,  88(sp)
    fld     fs2,  96(sp)
    fld     fs3, 104(sp)
    fld     fs4, 112(sp)
    fld     fs5, 120(sp)
    fld     fs6, 128(sp)
    fld     fs7, 136(sp)
    fld     fs8, 144(sp)
    fld     fs9, 152(sp)
    fld     fs10, 160(sp)
    fld     fs11, 168(sp)
    ld      t0,  72(sp)
    csrw    CSR_STATUS, t0
    ld      ra,   0(sp)
    ld      s0,   8(sp)
    ld      s1,  16(sp)
    ld      s2,  24(sp)
    ld      s3,  32(sp)
    ld      s4,  40(sp)
    ld      s5,  48(sp)
    ld      s6,  56(sp)
    addi    sp, sp, 176
    ret
    .size fpu_fs_yield_probe, . - fpu_fs_yield_probe

/*
 * int fpu_irq_preempt_probe(unsigned id, unsigned spin_units,
 *                           volatile unsigned *active,
 *                           struct fpu_fail *out)
 *
 * Load a full f0-f31 image plus fcsr, then spin in assembly (no function
 * calls) long enough for the timer interrupt to preempt this thread and
 * run its peer.  Because the spin contains no calls, any FPU register
 * change is caused by the trap/scheduling path, not by the compiler or
 * the ABI.
 *
 * Return: 0 on success, 1..32 for the first mismatching f register,
 *         33 for an fcsr mismatch.
 */
    .globl fpu_irq_preempt_probe
    .type fpu_irq_preempt_probe, @function
fpu_irq_preempt_probe:
    addi    sp, sp, -176
    sd      ra,   0(sp)
    sd      s0,   8(sp)
    sd      s1,  16(sp)
    sd      s2,  24(sp)
    sd      s3,  32(sp)
    sd      s4,  40(sp)
    sd      s5,  48(sp)
    csrr    t0, 0x003
    sd      t0,  56(sp)
    csrr    t0, CSR_STATUS
    sd      t0,  64(sp)
    fsd     fs0,  72(sp)
    fsd     fs1,  80(sp)
    fsd     fs2,  88(sp)
    fsd     fs3,  96(sp)
    fsd     fs4, 104(sp)
    fsd     fs5, 112(sp)
    fsd     fs6, 120(sp)
    fsd     fs7, 128(sp)
    fsd     fs8, 136(sp)
    fsd     fs9, 144(sp)
    fsd     fs10, 152(sp)
    fsd     fs11, 160(sp)

    mv      s0, a0              /* id */
    mv      s1, a1              /* spin_units */
    mv      s2, a2              /* active[] */
    mv      s3, a3              /* out */

    li      t0, SR_FS
    csrs    CSR_STATUS, t0

    /* Mark this worker as actively spinning. */
    slli    t0, s0, 2
    add     t0, t0, s2
    li      t1, 1
    sw      t1, 0(t0)

    /* Record whether the peer was already spinning when we started. */
    xori    t2, s0, 1
    slli    t2, t2, 2
    add     t2, t2, s2
    lw      t3, 0(t2)
    beqz    t3, .Lno_peer_seen
    beqz    s3, .Lno_peer_seen
    lw      t4, 12(s3)
    ori     t4, t4, 1
    sw      t4, 12(s3)
.Lno_peer_seen:

    /* Full FPU image: f0..f31 = base + index. */
    li      s4, 0x3ff0000000000000
    slli    t0, s0, 32
    add     s4, s4, t0
    addi    t0, s4, 0
    fmv.d.x f0, t0
    addi    t0, s4, 1
    fmv.d.x f1, t0
    addi    t0, s4, 2
    fmv.d.x f2, t0
    addi    t0, s4, 3
    fmv.d.x f3, t0
    addi    t0, s4, 4
    fmv.d.x f4, t0
    addi    t0, s4, 5
    fmv.d.x f5, t0
    addi    t0, s4, 6
    fmv.d.x f6, t0
    addi    t0, s4, 7
    fmv.d.x f7, t0
    addi    t0, s4, 8
    fmv.d.x f8, t0
    addi    t0, s4, 9
    fmv.d.x f9, t0
    addi    t0, s4, 10
    fmv.d.x f10, t0
    addi    t0, s4, 11
    fmv.d.x f11, t0
    addi    t0, s4, 12
    fmv.d.x f12, t0
    addi    t0, s4, 13
    fmv.d.x f13, t0
    addi    t0, s4, 14
    fmv.d.x f14, t0
    addi    t0, s4, 15
    fmv.d.x f15, t0
    addi    t0, s4, 16
    fmv.d.x f16, t0
    addi    t0, s4, 17
    fmv.d.x f17, t0
    addi    t0, s4, 18
    fmv.d.x f18, t0
    addi    t0, s4, 19
    fmv.d.x f19, t0
    addi    t0, s4, 20
    fmv.d.x f20, t0
    addi    t0, s4, 21
    fmv.d.x f21, t0
    addi    t0, s4, 22
    fmv.d.x f22, t0
    addi    t0, s4, 23
    fmv.d.x f23, t0
    addi    t0, s4, 24
    fmv.d.x f24, t0
    addi    t0, s4, 25
    fmv.d.x f25, t0
    addi    t0, s4, 26
    fmv.d.x f26, t0
    addi    t0, s4, 27
    fmv.d.x f27, t0
    addi    t0, s4, 28
    fmv.d.x f28, t0
    addi    t0, s4, 29
    fmv.d.x f29, t0
    addi    t0, s4, 30
    fmv.d.x f30, t0
    addi    t0, s4, 31
    fmv.d.x f31, t0

    /* Distinct fcsr per worker. */
    andi    t0, s0, 1
    bnez    t0, .Lirq_fcsr_odd
    li      s5, 0x68            /* frm=RUP, fflags=DZ */
    j       .Lirq_fcsr_set
.Lirq_fcsr_odd:
    li      s5, 0x24            /* frm=RTZ, fflags=OF */
.Lirq_fcsr_set:
    csrw    0x003, s5

    /*
     * Spin on rdtime.  No calls and no stack accesses in this loop, so a
     * timer preemption must preserve the live FPU image exactly.
     */
    rdtime  t0
    addi    t6, s1, 0
.Lirq_spin:
    rdtime  t1
    sub     t1, t1, t0
    bltu    t1, t6, .Lirq_spin

    /* Stop advertising this worker. */
    slli    t0, s0, 2
    add     t0, t0, s2
    sw      zero, 0(t0)

    /* Verify f0..f31. */
    fmv.x.d t2, f0
    addi    t3, s4, 0
    beq     t2, t3, .Lirq_ok0
    li      t4, 1
    j       .Lirq_fail
.Lirq_ok0:
    fmv.x.d t2, f1
    addi    t3, s4, 1
    beq     t2, t3, .Lirq_ok1
    li      t4, 2
    j       .Lirq_fail
.Lirq_ok1:
    fmv.x.d t2, f2
    addi    t3, s4, 2
    beq     t2, t3, .Lirq_ok2
    li      t4, 3
    j       .Lirq_fail
.Lirq_ok2:
    fmv.x.d t2, f3
    addi    t3, s4, 3
    beq     t2, t3, .Lirq_ok3
    li      t4, 4
    j       .Lirq_fail
.Lirq_ok3:
    fmv.x.d t2, f4
    addi    t3, s4, 4
    beq     t2, t3, .Lirq_ok4
    li      t4, 5
    j       .Lirq_fail
.Lirq_ok4:
    fmv.x.d t2, f5
    addi    t3, s4, 5
    beq     t2, t3, .Lirq_ok5
    li      t4, 6
    j       .Lirq_fail
.Lirq_ok5:
    fmv.x.d t2, f6
    addi    t3, s4, 6
    beq     t2, t3, .Lirq_ok6
    li      t4, 7
    j       .Lirq_fail
.Lirq_ok6:
    fmv.x.d t2, f7
    addi    t3, s4, 7
    beq     t2, t3, .Lirq_ok7
    li      t4, 8
    j       .Lirq_fail
.Lirq_ok7:
    fmv.x.d t2, f8
    addi    t3, s4, 8
    beq     t2, t3, .Lirq_ok8
    li      t4, 9
    j       .Lirq_fail
.Lirq_ok8:
    fmv.x.d t2, f9
    addi    t3, s4, 9
    beq     t2, t3, .Lirq_ok9
    li      t4, 10
    j       .Lirq_fail
.Lirq_ok9:
    fmv.x.d t2, f10
    addi    t3, s4, 10
    beq     t2, t3, .Lirq_ok10
    li      t4, 11
    j       .Lirq_fail
.Lirq_ok10:
    fmv.x.d t2, f11
    addi    t3, s4, 11
    beq     t2, t3, .Lirq_ok11
    li      t4, 12
    j       .Lirq_fail
.Lirq_ok11:
    fmv.x.d t2, f12
    addi    t3, s4, 12
    beq     t2, t3, .Lirq_ok12
    li      t4, 13
    j       .Lirq_fail
.Lirq_ok12:
    fmv.x.d t2, f13
    addi    t3, s4, 13
    beq     t2, t3, .Lirq_ok13
    li      t4, 14
    j       .Lirq_fail
.Lirq_ok13:
    fmv.x.d t2, f14
    addi    t3, s4, 14
    beq     t2, t3, .Lirq_ok14
    li      t4, 15
    j       .Lirq_fail
.Lirq_ok14:
    fmv.x.d t2, f15
    addi    t3, s4, 15
    beq     t2, t3, .Lirq_ok15
    li      t4, 16
    j       .Lirq_fail
.Lirq_ok15:
    fmv.x.d t2, f16
    addi    t3, s4, 16
    beq     t2, t3, .Lirq_ok16
    li      t4, 17
    j       .Lirq_fail
.Lirq_ok16:
    fmv.x.d t2, f17
    addi    t3, s4, 17
    beq     t2, t3, .Lirq_ok17
    li      t4, 18
    j       .Lirq_fail
.Lirq_ok17:
    fmv.x.d t2, f18
    addi    t3, s4, 18
    beq     t2, t3, .Lirq_ok18
    li      t4, 19
    j       .Lirq_fail
.Lirq_ok18:
    fmv.x.d t2, f19
    addi    t3, s4, 19
    beq     t2, t3, .Lirq_ok19
    li      t4, 20
    j       .Lirq_fail
.Lirq_ok19:
    fmv.x.d t2, f20
    addi    t3, s4, 20
    beq     t2, t3, .Lirq_ok20
    li      t4, 21
    j       .Lirq_fail
.Lirq_ok20:
    fmv.x.d t2, f21
    addi    t3, s4, 21
    beq     t2, t3, .Lirq_ok21
    li      t4, 22
    j       .Lirq_fail
.Lirq_ok21:
    fmv.x.d t2, f22
    addi    t3, s4, 22
    beq     t2, t3, .Lirq_ok22
    li      t4, 23
    j       .Lirq_fail
.Lirq_ok22:
    fmv.x.d t2, f23
    addi    t3, s4, 23
    beq     t2, t3, .Lirq_ok23
    li      t4, 24
    j       .Lirq_fail
.Lirq_ok23:
    fmv.x.d t2, f24
    addi    t3, s4, 24
    beq     t2, t3, .Lirq_ok24
    li      t4, 25
    j       .Lirq_fail
.Lirq_ok24:
    fmv.x.d t2, f25
    addi    t3, s4, 25
    beq     t2, t3, .Lirq_ok25
    li      t4, 26
    j       .Lirq_fail
.Lirq_ok25:
    fmv.x.d t2, f26
    addi    t3, s4, 26
    beq     t2, t3, .Lirq_ok26
    li      t4, 27
    j       .Lirq_fail
.Lirq_ok26:
    fmv.x.d t2, f27
    addi    t3, s4, 27
    beq     t2, t3, .Lirq_ok27
    li      t4, 28
    j       .Lirq_fail
.Lirq_ok27:
    fmv.x.d t2, f28
    addi    t3, s4, 28
    beq     t2, t3, .Lirq_ok28
    li      t4, 29
    j       .Lirq_fail
.Lirq_ok28:
    fmv.x.d t2, f29
    addi    t3, s4, 29
    beq     t2, t3, .Lirq_ok29
    li      t4, 30
    j       .Lirq_fail
.Lirq_ok29:
    fmv.x.d t2, f30
    addi    t3, s4, 30
    beq     t2, t3, .Lirq_ok30
    li      t4, 31
    j       .Lirq_fail
.Lirq_ok30:
    fmv.x.d t2, f31
    addi    t3, s4, 31
    beq     t2, t3, .Lirq_ok31
    li      t4, 32
    j       .Lirq_fail
.Lirq_ok31:

    csrr    t2, 0x003
    beq     t2, s5, .Lirq_success
    li      t4, 33
    mv      t3, s5
    j       .Lirq_fail

.Lirq_success:
    li      a0, 0
    j       .Lirq_epilogue

.Lirq_fail:
    beqz    s3, .Lirq_fail_ret
    sw      t4,   0(s3)         /* code */
    sw      zero, 4(s3)         /* round */
    addi    t5, t4, -1
    sw      t5,   8(s3)         /* register index (32 == fcsr) */
    sd      t3,  16(s3)         /* expected */
    sd      t2,  24(s3)         /* actual */
.Lirq_fail_ret:
    mv      a0, t4

.Lirq_epilogue:
    ld      t0,  56(sp)
    csrw    0x003, t0
    li      t0, SR_FS
    csrs    CSR_STATUS, t0
    fld     fs0,  72(sp)
    fld     fs1,  80(sp)
    fld     fs2,  88(sp)
    fld     fs3,  96(sp)
    fld     fs4, 104(sp)
    fld     fs5, 112(sp)
    fld     fs6, 120(sp)
    fld     fs7, 128(sp)
    fld     fs8, 136(sp)
    fld     fs9, 144(sp)
    fld     fs10, 152(sp)
    fld     fs11, 160(sp)
    ld      t0,  64(sp)
    csrw    CSR_STATUS, t0
    ld      ra,   0(sp)
    ld      s0,   8(sp)
    ld      s1,  16(sp)
    ld      s2,  24(sp)
    ld      s3,  32(sp)
    ld      s4,  40(sp)
    ld      s5,  48(sp)
    addi    sp, sp, 176
    ret
    .size fpu_irq_preempt_probe, . - fpu_irq_preempt_probe

/*
 * void fpu_read_state(unsigned long *out)
 *
 * Read fs0-fs11 into out[0..11] and fcsr into out[12] without modifying
 * any floating-point register.  Used to observe a new thread's initial
 * FPU state.
 */
    .globl fpu_read_state
    .type fpu_read_state, @function
fpu_read_state:
    fmv.x.d t0, fs0
    sd      t0,   0(a0)
    fmv.x.d t0, fs1
    sd      t0,   8(a0)
    fmv.x.d t0, fs2
    sd      t0,  16(a0)
    fmv.x.d t0, fs3
    sd      t0,  24(a0)
    fmv.x.d t0, fs4
    sd      t0,  32(a0)
    fmv.x.d t0, fs5
    sd      t0,  40(a0)
    fmv.x.d t0, fs6
    sd      t0,  48(a0)
    fmv.x.d t0, fs7
    sd      t0,  56(a0)
    fmv.x.d t0, fs8
    sd      t0,  64(a0)
    fmv.x.d t0, fs9
    sd      t0,  72(a0)
    fmv.x.d t0, fs10
    sd      t0,  80(a0)
    fmv.x.d t0, fs11
    sd      t0,  88(a0)
    csrr    t0, 0x003
    sd      t0,  96(a0)
    ret
    .size fpu_read_state, . - fpu_read_state

/*
 * void fpu_set_state_and_start(rt_thread_t tid, unsigned id)
 *
 * Load a distinctive live FPU image into the *creator*, then start a new
 * thread and yield while the image is still live.  The new thread can
 * therefore observe whether the kernel initialised its FPU frame or let
 * it inherit the creator's live registers.
 *
 * The caller's fs0-fs11 and fcsr are restored before returning.
 */
    .globl fpu_set_state_and_start
    .type fpu_set_state_and_start, @function
fpu_set_state_and_start:
    addi    sp, sp, -144
    sd      ra,   0(sp)
    sd      s0,   8(sp)
    sd      s1,  16(sp)
    csrr    t0, 0x003
    sd      t0, 120(sp)
    csrr    t0, CSR_STATUS
    sd      t0, 128(sp)
    fsd     fs0,  24(sp)
    fsd     fs1,  32(sp)
    fsd     fs2,  40(sp)
    fsd     fs3,  48(sp)
    fsd     fs4,  56(sp)
    fsd     fs5,  64(sp)
    fsd     fs6,  72(sp)
    fsd     fs7,  80(sp)
    fsd     fs8,  88(sp)
    fsd     fs9,  96(sp)
    fsd     fs10, 104(sp)
    fsd     fs11, 112(sp)

    mv      s0, a0              /* tid */
    mv      s1, a1              /* id */

    li      t0, SR_FS
    csrs    CSR_STATUS, t0

    li      t1, 0x3ff0000000000000
    slli    t0, s1, 32
    add     t1, t1, t0
    fmv.d.x fs0, t1
    addi    t2, t1, 1
    fmv.d.x fs1, t2
    addi    t2, t1, 2
    fmv.d.x fs2, t2
    addi    t2, t1, 3
    fmv.d.x fs3, t2
    addi    t2, t1, 4
    fmv.d.x fs4, t2
    addi    t2, t1, 5
    fmv.d.x fs5, t2
    addi    t2, t1, 6
    fmv.d.x fs6, t2
    addi    t2, t1, 7
    fmv.d.x fs7, t2
    addi    t2, t1, 8
    fmv.d.x fs8, t2
    addi    t2, t1, 9
    fmv.d.x fs9, t2
    addi    t2, t1, 10
    fmv.d.x fs10, t2
    addi    t2, t1, 11
    fmv.d.x fs11, t2

    li      t0, 0x68            /* frm=RUP, fflags=DZ */
    csrw    0x003, t0

    mv      a0, s0
    call    rt_thread_startup
    call    rt_thread_yield

    ld      t0, 120(sp)
    csrw    0x003, t0
    li      t0, SR_FS
    csrs    CSR_STATUS, t0
    fld     fs0,  24(sp)
    fld     fs1,  32(sp)
    fld     fs2,  40(sp)
    fld     fs3,  48(sp)
    fld     fs4,  56(sp)
    fld     fs5,  64(sp)
    fld     fs6,  72(sp)
    fld     fs7,  80(sp)
    fld     fs8,  88(sp)
    fld     fs9,  96(sp)
    fld     fs10, 104(sp)
    fld     fs11, 112(sp)
    ld      t0, 128(sp)
    csrw    CSR_STATUS, t0
    ld      ra,   0(sp)
    ld      s0,   8(sp)
    ld      s1,  16(sp)
    addi    sp, sp, 144
    ret
    .size fpu_set_state_and_start, . - fpu_set_state_and_start
