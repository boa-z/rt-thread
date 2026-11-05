/*
 * Copyright (c) 2006-2025, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-10-28     Bernard      The unify RISC-V porting implementation
 * 2018-12-27     Jesven       Add SMP support
 * 2021-02-02     lizhirui     Add userspace support
 * 2022-10-22     Shell        Support User mode RVV;
 *                             Trimming process switch context
 * 2025-04-20     GuEe-GUI     Support SMP
 */

#include "asm.h"
#include "csr.h"

#define REG_IDX(name)   RT_HW_SWITCH_CONTEXT_##name

#ifdef ARCH_RISCV_FPU
#define CSR_FCSR                    0x003
#define CTX_SWITCH_FPU_OFF          (RT_HW_SWITCH_CONTEXT_SIZE * SZREG)
#define CTX_SWITCH_FCSR_OFF         (CTX_SWITCH_FPU_OFF + CTX_FPU_REG_NR * SZREG)
#define CTX_SWITCH_SIZE             ((CTX_SWITCH_FCSR_OFF + SZREG + 15) & ~15)
#else
#define CTX_SWITCH_SIZE             (RT_HW_SWITCH_CONTEXT_SIZE * SZREG)
#endif

.macro SAVE_REG reg, index
    REG_S   \reg, \index * SZREG(sp)
.endm

.macro LOAD_REG reg, index
    REG_L   \reg, \index * SZREG(sp)
.endm

.macro SAVE_CONTEXT
    addi        sp, sp, -CTX_SWITCH_SIZE
    SAVE_REG    tp, REG_IDX(TP)
    SAVE_REG    ra, REG_IDX(RA)
    SAVE_REG    s0, REG_IDX(S0)
    SAVE_REG    s1, REG_IDX(S1)
#ifndef ARCH_RISCV_32E
    SAVE_REG    s2, REG_IDX(S2)
    SAVE_REG    s3, REG_IDX(S3)
    SAVE_REG    s4, REG_IDX(S4)
    SAVE_REG    s5, REG_IDX(S5)
    SAVE_REG    s6, REG_IDX(S6)
    SAVE_REG    s7, REG_IDX(S7)
    SAVE_REG    s8, REG_IDX(S8)
    SAVE_REG    s9, REG_IDX(S9)
    SAVE_REG    s10, REG_IDX(S10)
    SAVE_REG    s11, REG_IDX(S11)
#endif
    csrr        s1, CSR_STATUS
    li          s0, (SR_PP)
#ifdef ARCH_RISCV_FPU
    /* Keep the FPU enabled in the restored status. */
    li          t0, SR_FS
    or          s1, s1, t0
    csrs        CSR_STATUS, t0
#endif
    or          s1, s1, s0
    SAVE_REG    s1, REG_IDX(XSTATUS)

#ifdef ARCH_RISCV_FPU
    CTX_FREG_S  f0,  (CTX_SWITCH_FPU_OFF + 0 * SZREG)(sp)
    CTX_FREG_S  f1,  (CTX_SWITCH_FPU_OFF + 1 * SZREG)(sp)
    CTX_FREG_S  f2,  (CTX_SWITCH_FPU_OFF + 2 * SZREG)(sp)
    CTX_FREG_S  f3,  (CTX_SWITCH_FPU_OFF + 3 * SZREG)(sp)
    CTX_FREG_S  f4,  (CTX_SWITCH_FPU_OFF + 4 * SZREG)(sp)
    CTX_FREG_S  f5,  (CTX_SWITCH_FPU_OFF + 5 * SZREG)(sp)
    CTX_FREG_S  f6,  (CTX_SWITCH_FPU_OFF + 6 * SZREG)(sp)
    CTX_FREG_S  f7,  (CTX_SWITCH_FPU_OFF + 7 * SZREG)(sp)
    CTX_FREG_S  f8,  (CTX_SWITCH_FPU_OFF + 8 * SZREG)(sp)
    CTX_FREG_S  f9,  (CTX_SWITCH_FPU_OFF + 9 * SZREG)(sp)
    CTX_FREG_S  f10, (CTX_SWITCH_FPU_OFF + 10 * SZREG)(sp)
    CTX_FREG_S  f11, (CTX_SWITCH_FPU_OFF + 11 * SZREG)(sp)
    CTX_FREG_S  f12, (CTX_SWITCH_FPU_OFF + 12 * SZREG)(sp)
    CTX_FREG_S  f13, (CTX_SWITCH_FPU_OFF + 13 * SZREG)(sp)
    CTX_FREG_S  f14, (CTX_SWITCH_FPU_OFF + 14 * SZREG)(sp)
    CTX_FREG_S  f15, (CTX_SWITCH_FPU_OFF + 15 * SZREG)(sp)
    CTX_FREG_S  f16, (CTX_SWITCH_FPU_OFF + 16 * SZREG)(sp)
    CTX_FREG_S  f17, (CTX_SWITCH_FPU_OFF + 17 * SZREG)(sp)
    CTX_FREG_S  f18, (CTX_SWITCH_FPU_OFF + 18 * SZREG)(sp)
    CTX_FREG_S  f19, (CTX_SWITCH_FPU_OFF + 19 * SZREG)(sp)
    CTX_FREG_S  f20, (CTX_SWITCH_FPU_OFF + 20 * SZREG)(sp)
    CTX_FREG_S  f21, (CTX_SWITCH_FPU_OFF + 21 * SZREG)(sp)
    CTX_FREG_S  f22, (CTX_SWITCH_FPU_OFF + 22 * SZREG)(sp)
    CTX_FREG_S  f23, (CTX_SWITCH_FPU_OFF + 23 * SZREG)(sp)
    CTX_FREG_S  f24, (CTX_SWITCH_FPU_OFF + 24 * SZREG)(sp)
    CTX_FREG_S  f25, (CTX_SWITCH_FPU_OFF + 25 * SZREG)(sp)
    CTX_FREG_S  f26, (CTX_SWITCH_FPU_OFF + 26 * SZREG)(sp)
    CTX_FREG_S  f27, (CTX_SWITCH_FPU_OFF + 27 * SZREG)(sp)
    CTX_FREG_S  f28, (CTX_SWITCH_FPU_OFF + 28 * SZREG)(sp)
    CTX_FREG_S  f29, (CTX_SWITCH_FPU_OFF + 29 * SZREG)(sp)
    CTX_FREG_S  f30, (CTX_SWITCH_FPU_OFF + 30 * SZREG)(sp)
    CTX_FREG_S  f31, (CTX_SWITCH_FPU_OFF + 31 * SZREG)(sp)
    csrr        t0, CSR_FCSR
    REG_S       t0, CTX_SWITCH_FCSR_OFF(sp)
#endif
.endm

.macro RESTORE_CONTEXT
    LOAD_REG    s1, REG_IDX(XSTATUS)
    csrw        CSR_STATUS, s1
#ifndef ARCH_RISCV_32E
    LOAD_REG    s11, REG_IDX(S11)
    LOAD_REG    s10, REG_IDX(S10)
    LOAD_REG    s9, REG_IDX(S9)
    LOAD_REG    s8, REG_IDX(S8)
    LOAD_REG    s7, REG_IDX(S7)
    LOAD_REG    s6, REG_IDX(S6)
    LOAD_REG    s5, REG_IDX(S5)
    LOAD_REG    s4, REG_IDX(S4)
    LOAD_REG    s3, REG_IDX(S3)
    LOAD_REG    s2, REG_IDX(S2)
#endif
    LOAD_REG    s1, REG_IDX(S1)
    LOAD_REG    s0, REG_IDX(S0)
    LOAD_REG    ra, REG_IDX(RA)

#ifdef ARCH_RISCV_FPU
    CTX_FREG_L  f0,  (CTX_SWITCH_FPU_OFF + 0 * SZREG)(sp)
    CTX_FREG_L  f1,  (CTX_SWITCH_FPU_OFF + 1 * SZREG)(sp)
    CTX_FREG_L  f2,  (CTX_SWITCH_FPU_OFF + 2 * SZREG)(sp)
    CTX_FREG_L  f3,  (CTX_SWITCH_FPU_OFF + 3 * SZREG)(sp)
    CTX_FREG_L  f4,  (CTX_SWITCH_FPU_OFF + 4 * SZREG)(sp)
    CTX_FREG_L  f5,  (CTX_SWITCH_FPU_OFF + 5 * SZREG)(sp)
    CTX_FREG_L  f6,  (CTX_SWITCH_FPU_OFF + 6 * SZREG)(sp)
    CTX_FREG_L  f7,  (CTX_SWITCH_FPU_OFF + 7 * SZREG)(sp)
    CTX_FREG_L  f8,  (CTX_SWITCH_FPU_OFF + 8 * SZREG)(sp)
    CTX_FREG_L  f9,  (CTX_SWITCH_FPU_OFF + 9 * SZREG)(sp)
    CTX_FREG_L  f10, (CTX_SWITCH_FPU_OFF + 10 * SZREG)(sp)
    CTX_FREG_L  f11, (CTX_SWITCH_FPU_OFF + 11 * SZREG)(sp)
    CTX_FREG_L  f12, (CTX_SWITCH_FPU_OFF + 12 * SZREG)(sp)
    CTX_FREG_L  f13, (CTX_SWITCH_FPU_OFF + 13 * SZREG)(sp)
    CTX_FREG_L  f14, (CTX_SWITCH_FPU_OFF + 14 * SZREG)(sp)
    CTX_FREG_L  f15, (CTX_SWITCH_FPU_OFF + 15 * SZREG)(sp)
    CTX_FREG_L  f16, (CTX_SWITCH_FPU_OFF + 16 * SZREG)(sp)
    CTX_FREG_L  f17, (CTX_SWITCH_FPU_OFF + 17 * SZREG)(sp)
    CTX_FREG_L  f18, (CTX_SWITCH_FPU_OFF + 18 * SZREG)(sp)
    CTX_FREG_L  f19, (CTX_SWITCH_FPU_OFF + 19 * SZREG)(sp)
    CTX_FREG_L  f20, (CTX_SWITCH_FPU_OFF + 20 * SZREG)(sp)
    CTX_FREG_L  f21, (CTX_SWITCH_FPU_OFF + 21 * SZREG)(sp)
    CTX_FREG_L  f22, (CTX_SWITCH_FPU_OFF + 22 * SZREG)(sp)
    CTX_FREG_L  f23, (CTX_SWITCH_FPU_OFF + 23 * SZREG)(sp)
    CTX_FREG_L  f24, (CTX_SWITCH_FPU_OFF + 24 * SZREG)(sp)
    CTX_FREG_L  f25, (CTX_SWITCH_FPU_OFF + 25 * SZREG)(sp)
    CTX_FREG_L  f26, (CTX_SWITCH_FPU_OFF + 26 * SZREG)(sp)
    CTX_FREG_L  f27, (CTX_SWITCH_FPU_OFF + 27 * SZREG)(sp)
    CTX_FREG_L  f28, (CTX_SWITCH_FPU_OFF + 28 * SZREG)(sp)
    CTX_FREG_L  f29, (CTX_SWITCH_FPU_OFF + 29 * SZREG)(sp)
    CTX_FREG_L  f30, (CTX_SWITCH_FPU_OFF + 30 * SZREG)(sp)
    CTX_FREG_L  f31, (CTX_SWITCH_FPU_OFF + 31 * SZREG)(sp)
    REG_L       t0, CTX_SWITCH_FCSR_OFF(sp)
    csrw        CSR_FCSR, t0
#endif
    RESTORE_TP (REG_IDX(TP) * SZREG)
    addi        sp, sp, CTX_SWITCH_SIZE
    csrw        CSR_EPC, ra
.endm

    .section ".text","ax"

#ifdef RT_USING_SMP
/*
 * void rt_hw_context_switch_to(rt_ubase_t to, struct rt_thread *to_thread);
 * a0 --> to (thread stack)
 * a1 --> to_thread
 */
.globl rt_hw_context_switch_to
rt_hw_context_switch_to:
    REG_L   sp, (a0)

    /* Reserved to_thread */
    mv      s1, a1

    mv      a0, s1
    call    rt_cpus_lock_status_restore
#ifdef RT_USING_SMART
    mv      a0, s1
    call    lwp_user_setting_restore
#endif

    j       _context_switch_exit

/*
 * void rt_hw_context_switch(rt_ubase_t from, rt_ubase_t to, struct rt_thread *to_thread);
 * a0 --> from (from_thread stack)
 * a1 --> to (to_thread stack)
 * a2 --> to_thread
 */
.globl rt_hw_context_switch
rt_hw_context_switch:
    SAVE_CONTEXT
    mv      a3, sp
    REG_S   a3, (a0)            /* Store sp in preempted tasks TCB */
    REG_L   a0, (a1)            /* Get new task stack pointer */
    mv      sp, a0

    /* Backup thread self */
    mv      s1, a2

    mv      a0, s1
    call    rt_cpus_lock_status_restore
#ifdef RT_USING_SMART
    mv      a0, s1
    call    lwp_user_setting_restore
#endif
    j       _context_switch_exit

#define EXP_FRAME   t0
#define FROM_SPP    t1
#define TO_SPP      t2
#define TO_TCB      a4
#define TMP         a5

/*
 * void rt_hw_context_switch_interrupt(rt_ubase_t context, rt_ubase_t from, rt_ubase_t to, struct rt_thread *to_thread)
 * a0 --> interrupt context
 * a1 --> addr of from_thread's sp
 * a2 --> addr of to_thread's sp
 * a3 --> to_thread's tcb
 */
.globl rt_hw_context_switch_interrupt
rt_hw_context_switch_interrupt:
#ifdef RT_USING_DEBUG
    /* Debug frame for backtrace */
#if __riscv_xlen == 64
    addi    sp, sp, -(48)
    sd      ra, 40(sp)
    sd      s0, 32(sp)
#elif __riscv_xlen == 32
    addi    sp, sp, -(32)
    sw      ra, 28(sp)
    sw      s0, 24(sp)
#else
#error "Unexpected __riscv_xlen"
#endif
#endif /* RT_USING_DEBUG */

    /* We can discard all the previous ABI here */
    mv      EXP_FRAME, a0
    mv      FROM_SPP, a1
    mv      TO_SPP, a2
    mv      TO_TCB, a3

#ifdef RT_USING_SMART
    PUSH    EXP_FRAME
    PUSH    FROM_SPP
    PUSH    TO_SPP
    PUSH    TO_TCB

    call    rt_thread_self
    call    lwp_user_setting_save

    POP     TO_TCB
    POP     TO_SPP
    POP     FROM_SPP
    POP     EXP_FRAME
#endif /* RT_USING_SMART */

    /* Reset SP of from-thread */
    mv      sp, EXP_FRAME

    addi    sp, sp, -(RT_HW_SWITCH_CONTEXT_SIZE * SZREG)
    /* Resume according to the privilege mode saved in the exception frame. */
    la      ra, rt_hw_resume_interrupt
    /* Save the entire context */
    li      TMP, (K_XSTATUS_DEFAULT & ~SR_PIE)
    REG_S   TMP, REG_IDX(XSTATUS) * SZREG(sp)
    REG_S   ra, REG_IDX(RA) * SZREG(sp)

    /* Save SP of from-thread */
    mv      a0, sp
    REG_S   a0, (FROM_SPP)

    /* Setup SP to to-thread's */
    REG_L   a0, (TO_SPP)
    mv      sp, a0

#ifdef RT_USING_SMART
    PUSH    TO_TCB
#endif
    mv      a0, TO_TCB
    call    rt_cpus_lock_status_restore
#ifdef RT_USING_SMART
    POP     a0  /* TO_TCB */
    call    lwp_user_setting_restore
#endif
    j       _context_switch_exit

#else /* !RT_USING_SMP */
    .section ".data"

    .align RISCV_SZPTR
/**
 * @brief from thread used interrupt context switch
 */
.globl rt_interrupt_from_thread
rt_interrupt_from_thread:
    RISCV_PTR 0

/**
 * @brief to thread used interrupt context switch
 */
.globl rt_interrupt_to_thread
rt_interrupt_to_thread:
    RISCV_PTR 0

/**
 * @brief flag to indicate context switch in interrupt or not
 */
.globl rt_thread_switch_interrupt_flag
rt_thread_switch_interrupt_flag:
    RISCV_PTR 0

    .section ".text","ax"

/*
 * void rt_hw_context_switch_to(rt_ubase_t to);
 *
 * a0 --> to SP pointer
 */
.globl rt_hw_context_switch_to
rt_hw_context_switch_to:
    REG_L   sp, (a0)

    call    rt_thread_self
    mv      s1, a0

#ifdef RT_USING_SMART
    call    lwp_aspace_switch
#endif

    j       _context_switch_exit

/*
 * void rt_hw_context_switch(rt_ubase_t from, rt_ubase_t to);
 *
 * a0 --> from SP pointer
 * a1 --> to SP pointer
 *
 * It should only be used on local interrupt disable
 */
.globl rt_hw_context_switch
rt_hw_context_switch:
    SAVE_CONTEXT
    REG_S   sp, (a0)

    /* Restore to thread SP */
    REG_L   sp, (a1)

    /* Restore Address Space */
    call    rt_thread_self
    mv      s1, a0

#ifdef RT_USING_SMART
    call    lwp_aspace_switch
#endif

    j       _context_switch_exit

/*
 * void rt_hw_context_switch_interrupt(rt_ubase_t from, rt_ubase_t to, rt_thread_t from_thread, rt_thread_t to_thread)
 * a0 --> from_thread's sp
 * a1 --> to_thread's sp
 * a2 --> from_thread's tcb
 * a3 --> to_thread's tcb
 */
.globl rt_hw_context_switch_interrupt
rt_hw_context_switch_interrupt:
    la      t0, rt_thread_switch_interrupt_flag
    REG_L   t1, (t0)
    bne     t1, zero, _init_to_thread

    la      t1, rt_interrupt_from_thread
    REG_S   a0, (t1)

_init_to_thread:
    la      t1, rt_interrupt_to_thread
    REG_S   a1, (t1)

    li      t1, 1
    REG_S   t1, (t0)

    ret
#endif /* RT_USING_SMP */

.local _context_switch_exit
_context_switch_exit:
    RESTORE_CONTEXT
    XRET
