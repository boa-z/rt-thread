/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Independent RISC-V FPU context regression tests for the QEMU virt64 BSP.
 *
 * Run with: utest_run testcases.libcpu.fpu_context
 *
 * The test is intentionally split into four independent scenarios:
 *
 *   A. voluntary switch: two FPU-using threads call rt_thread_yield()
 *      while fs0-fs11 hold per-thread markers.  fs0-fs11 are callee-saved,
 *      so a correct context switch must preserve them.
 *
 *   B. timer preemption: two FPU-using threads spin in assembly with no
 *      function calls while the timer interrupt preempts them.  The
 *      interrupted stream's complete f0-f31 and fcsr must be preserved.
 *
 *   C. mixed threads: an FPU-using thread yields to an integer-only peer.
 *      This verifies that the kernel does not corrupt FPU state by itself
 *      and shows that scenario A requires an FPU-using peer.
 *
 *   D. initial context: a newly created thread observes its FPU registers
 *      before touching them.  The integer part of a new thread frame is
 *      zeroed by rt_hw_stack_init(); a correct FPU frame should be
 *      deterministic as well.
 *
 * Results are printed before any uassert so that a failure in one scenario
 * does not hide the others.
 */

#include <rtthread.h>
#include <utest.h>
#include <stddef.h>

#define FPU_UTEST_ROUNDS        200
#define FPU_UTEST_IRQ_SPIN_MS   300
#define FPU_UTEST_IRQ_SPIN_UNITS (FPU_UTEST_IRQ_SPIN_MS * 10000) /* 10 MHz QEMU timebase */
#define FPU_UTEST_STACK         4096
/*
 * The workers must be lower priority than the UTest thread (priority 20)
 * so that starting the first worker cannot preempt the harness before the
 * second worker has been started.  Both workers then become runnable and
 * the harness blocks in rt_sem_take(), which is what makes the two
 * scenarios actually overlap.
 */
#define FPU_UTEST_PRIO          25
#define FPU_UTEST_IRQ_PRIO      24
#define FPU_UTEST_INIT_PRIO     5

#define TEST_PASS               0
#define TEST_FAIL               1
#define TEST_INCONCLUSIVE       2

struct fpu_fail
{
    rt_uint32_t code;
    rt_uint32_t round;
    rt_uint32_t reg;
    rt_uint32_t flags;
    rt_uint64_t expected;
    rt_uint64_t actual;
    rt_uint64_t fcsr_expected;
    rt_uint64_t fcsr_actual;
};

RT_STATIC_ASSERT(fpu_fail_expected_off, offsetof(struct fpu_fail, expected) == 16);
RT_STATIC_ASSERT(fpu_fail_actual_off, offsetof(struct fpu_fail, actual) == 24);
RT_STATIC_ASSERT(fpu_fail_size, sizeof(struct fpu_fail) == 48);

struct worker_ctx
{
    rt_uint32_t id;
    volatile rt_uint32_t entered;
    volatile rt_uint32_t stop;
    volatile rt_uint32_t saw_peer;
    rt_uint32_t yields;
    int rc;
    struct fpu_fail fail;
    struct worker_ctx *peer;
    struct rt_semaphore *done;
    volatile unsigned *active;
};

extern int fpu_fs_yield_probe(unsigned id, unsigned rounds,
                              void (*yield_fn)(void *), void *arg,
                              struct fpu_fail *out);
extern int fpu_irq_preempt_probe(unsigned id, unsigned spin_units,
                                 volatile unsigned *active,
                                 struct fpu_fail *out);
extern void fpu_read_state(unsigned long *out);
extern void fpu_set_state_and_start(rt_thread_t tid, unsigned id);

static struct rt_semaphore fpu_done;

static const char *test_result_name(int result)
{
    switch (result)
    {
    case TEST_PASS:
        return "PASS";
    case TEST_FAIL:
        return "FAIL";
    default:
        return "INCONCLUSIVE";
    }
}

static void print_u64(const char *name, rt_uint64_t value)
{
    rt_kprintf("%s=0x%08x%08x", name,
               (unsigned int)(value >> 32), (unsigned int)value);
}

static void print_fail(const char *test, const struct worker_ctx *ctx)
{
    const struct fpu_fail *f = &ctx->fail;

    rt_kprintf("%s: thread=%u code=%u round=%u reg=%u ",
               test, (unsigned int)ctx->id, (unsigned int)f->code,
               (unsigned int)f->round, (unsigned int)f->reg);
    print_u64("expected", f->expected);
    rt_kprintf(" ");
    print_u64("actual", f->actual);
    rt_kprintf(" yields=%u saw_peer=%u\n",
               (unsigned int)ctx->yields, (unsigned int)ctx->saw_peer);
}

static void test_yield(void *parameter)
{
    struct worker_ctx *ctx = parameter;

    ctx->yields++;
    rt_thread_yield();

    if (ctx->peer->entered)
    {
        ctx->saw_peer = 1;
    }
}

static void yield_worker(void *parameter)
{
    struct worker_ctx *ctx = parameter;

    ctx->entered = 1;
    ctx->rc = fpu_fs_yield_probe(ctx->id, FPU_UTEST_ROUNDS,
                                 test_yield, ctx, &ctx->fail);
    rt_sem_release(ctx->done);
}

static int run_voluntary_test(void)
{
    rt_thread_t t0;
    rt_thread_t t1;
    struct worker_ctx ctx0;
    struct worker_ctx ctx1;
    int result = TEST_PASS;

    rt_memset(&ctx0, 0, sizeof(ctx0));
    rt_memset(&ctx1, 0, sizeof(ctx1));
    ctx0.id = 0;
    ctx1.id = 1;
    ctx0.peer = &ctx1;
    ctx1.peer = &ctx0;
    ctx0.done = &fpu_done;
    ctx1.done = &fpu_done;

    rt_sem_init(&fpu_done, "fpuA", 0, RT_IPC_FLAG_PRIO);

    t0 = rt_thread_create("fpuA0", yield_worker, &ctx0,
                          FPU_UTEST_STACK, FPU_UTEST_PRIO, 10);
    t1 = rt_thread_create("fpuA1", yield_worker, &ctx1,
                          FPU_UTEST_STACK, FPU_UTEST_PRIO, 10);
    if (t0 == RT_NULL || t1 == RT_NULL)
    {
        rt_kprintf("A: thread create failed\n");
        rt_sem_detach(&fpu_done);
        return TEST_INCONCLUSIVE;
    }

    rt_thread_startup(t0);
    rt_thread_startup(t1);
    rt_sem_take(&fpu_done, RT_WAITING_FOREVER);
    rt_sem_take(&fpu_done, RT_WAITING_FOREVER);

    if (ctx0.rc != 0)
    {
        print_fail("A", &ctx0);
        result = TEST_FAIL;
    }
    if (ctx1.rc != 0)
    {
        print_fail("A", &ctx1);
        result = TEST_FAIL;
    }
    if (ctx0.rc == 0 && ctx1.rc == 0 &&
        !(ctx0.saw_peer && ctx1.saw_peer))
    {
        rt_kprintf("A: INCONCLUSIVE (peer did not run while FS values were live)\n");
        result = TEST_INCONCLUSIVE;
    }

    rt_kprintf("A: voluntary switch fs0-fs11 %s (rounds=%u yields=%u/%u)\n",
               test_result_name(result), FPU_UTEST_ROUNDS,
               (unsigned int)ctx0.yields, (unsigned int)ctx1.yields);

    rt_sem_detach(&fpu_done);
    return result;
}

static void irq_worker(void *parameter)
{
    struct worker_ctx *ctx = parameter;

    ctx->entered = 1;
    ctx->rc = fpu_irq_preempt_probe(ctx->id, FPU_UTEST_IRQ_SPIN_UNITS,
                                    ctx->active, &ctx->fail);
    rt_sem_release(ctx->done);
}

static int run_irq_test(void)
{
    rt_thread_t t0;
    rt_thread_t t1;
    struct worker_ctx ctx0;
    struct worker_ctx ctx1;
    volatile unsigned active[2] = { 0, 0 };
    int report_seen = 0;
    int result = TEST_PASS;

    rt_memset(&ctx0, 0, sizeof(ctx0));
    rt_memset(&ctx1, 0, sizeof(ctx1));
    ctx0.id = 0;
    ctx1.id = 1;
    ctx0.peer = &ctx1;
    ctx1.peer = &ctx0;
    ctx0.done = &fpu_done;
    ctx1.done = &fpu_done;
    ctx0.active = active;
    ctx1.active = active;

    rt_sem_init(&fpu_done, "fpuB", 0, RT_IPC_FLAG_PRIO);

    /*
     * Both workers use the same priority and a one-tick time slice so the
     * timer interrupt must alternate between them.  Each probe reads the
     * shared active[] array to prove that the peer was spinning while its
     * own FPU image was live.
     */
    t0 = rt_thread_create("fpuB0", irq_worker, &ctx0,
                          FPU_UTEST_STACK, FPU_UTEST_IRQ_PRIO, 1);
    t1 = rt_thread_create("fpuB1", irq_worker, &ctx1,
                          FPU_UTEST_STACK, FPU_UTEST_IRQ_PRIO, 1);
    if (t0 == RT_NULL || t1 == RT_NULL)
    {
        rt_kprintf("B: thread create failed\n");
        rt_sem_detach(&fpu_done);
        return TEST_INCONCLUSIVE;
    }

    rt_thread_startup(t0);
    rt_thread_startup(t1);
    rt_sem_take(&fpu_done, RT_WAITING_FOREVER);
    rt_sem_take(&fpu_done, RT_WAITING_FOREVER);

    report_seen = ((ctx0.fail.flags & 1) != 0) || ((ctx1.fail.flags & 1) != 0);

    if (ctx0.rc != 0)
    {
        print_fail("B", &ctx0);
        result = TEST_FAIL;
    }
    if (ctx1.rc != 0)
    {
        print_fail("B", &ctx1);
        result = TEST_FAIL;
    }
    if (!report_seen)
    {
        rt_kprintf("B: INCONCLUSIVE (no timer preemption overlapped the spin)\n");
        if (result == TEST_PASS)
        {
            result = TEST_INCONCLUSIVE;
        }
    }

    rt_kprintf("B: timer preemption f0-f31+fcsr %s (spin=%ums saw_peer=%u/%u)\n",
               test_result_name(result), FPU_UTEST_IRQ_SPIN_MS,
               (unsigned int)(ctx0.fail.flags & 1), (unsigned int)(ctx1.fail.flags & 1));

    rt_sem_detach(&fpu_done);
    return result;
}

static void mixed_fpu_worker(void *parameter)
{
    struct worker_ctx *ctx = parameter;

    ctx->entered = 1;
    ctx->rc = fpu_fs_yield_probe(ctx->id, FPU_UTEST_ROUNDS,
                                 test_yield, ctx, &ctx->fail);
    ctx->peer->stop = 1;
    rt_sem_release(ctx->done);
}

static void mixed_int_worker(void *parameter)
{
    struct worker_ctx *ctx = parameter;

    ctx->entered = 1;
    while (!ctx->stop)
    {
        ctx->yields++;
        rt_thread_yield();
    }
    rt_sem_release(ctx->done);
}

static int run_mixed_test(void)
{
    rt_thread_t t0;
    rt_thread_t t1;
    struct worker_ctx ctx0;
    struct worker_ctx ctx1;
    int result = TEST_PASS;

    rt_memset(&ctx0, 0, sizeof(ctx0));
    rt_memset(&ctx1, 0, sizeof(ctx1));
    ctx0.id = 0;
    ctx1.id = 1;
    ctx0.peer = &ctx1;
    ctx1.peer = &ctx0;
    ctx0.done = &fpu_done;
    ctx1.done = &fpu_done;

    rt_sem_init(&fpu_done, "fpuC", 0, RT_IPC_FLAG_PRIO);

    t0 = rt_thread_create("fpuC0", mixed_fpu_worker, &ctx0,
                          FPU_UTEST_STACK, FPU_UTEST_PRIO, 10);
    t1 = rt_thread_create("fpuC1", mixed_int_worker, &ctx1,
                          FPU_UTEST_STACK, FPU_UTEST_PRIO, 10);
    if (t0 == RT_NULL || t1 == RT_NULL)
    {
        rt_kprintf("C: thread create failed\n");
        rt_sem_detach(&fpu_done);
        return TEST_INCONCLUSIVE;
    }

    rt_thread_startup(t0);
    rt_thread_startup(t1);
    rt_sem_take(&fpu_done, RT_WAITING_FOREVER);
    rt_sem_take(&fpu_done, RT_WAITING_FOREVER);

    if (ctx0.rc != 0)
    {
        print_fail("C", &ctx0);
        result = TEST_FAIL;
    }
    if (!ctx0.saw_peer)
    {
        rt_kprintf("C: INCONCLUSIVE (integer peer did not run during yield)\n");
        if (result == TEST_PASS)
        {
            result = TEST_INCONCLUSIVE;
        }
    }

    rt_kprintf("C: mixed FPU/integer threads %s (int_yields=%u)\n",
               test_result_name(result), (unsigned int)ctx1.yields);

    rt_sem_detach(&fpu_done);
    return result;
}

static unsigned long init_state[13];
static struct rt_semaphore init_done;

static void initial_probe_thread(void *parameter)
{
    (void)parameter;
    fpu_read_state(init_state);
    rt_sem_release(&init_done);
}

static int run_initial_test(void)
{
    rt_thread_t t;
    int result = TEST_PASS;
    int i;

    rt_memset(init_state, 0, sizeof(init_state));
    rt_sem_init(&init_done, "fpuD", 0, RT_IPC_FLAG_PRIO);

    t = rt_thread_create("fpuD", initial_probe_thread, RT_NULL,
                         2048, FPU_UTEST_INIT_PRIO, 10);
    if (t == RT_NULL)
    {
        rt_kprintf("D: thread create failed\n");
        rt_sem_detach(&init_done);
        return TEST_INCONCLUSIVE;
    }

    /*
     * Load a live marker image in this thread, start the new thread while
     * those registers are still live, and let it read its own state.
     */
    fpu_set_state_and_start(t, 0);

    if (rt_sem_take(&init_done, rt_tick_from_millisecond(1000)) != RT_EOK)
    {
        rt_kprintf("D: TIMEOUT waiting for initial-state thread\n");
        rt_sem_detach(&init_done);
        return TEST_INCONCLUSIVE;
    }

    for (i = 0; i < 13; i++)
    {
        if (init_state[i] != 0)
        {
            result = TEST_FAIL;
            break;
        }
    }

    if (result == TEST_FAIL)
    {
        rt_kprintf("D: new thread inherited a non-zero FPU image: ");
        for (i = 0; i < 12; i++)
        {
            print_u64("fs", init_state[i]);
            rt_kprintf(" ");
        }
        print_u64("fcsr", init_state[12]);
        rt_kprintf("\n");
    }

    rt_kprintf("D: initial FPU context %s (expected all zero)\n",
               test_result_name(result));

    rt_sem_detach(&init_done);
    return result;
}

static void fpu_context_testcase(void)
{
    int a;
    int b;
    int c;
    int d;

    rt_kprintf("\nFPU context test: RV64D, utest auto-run, tick=%dHz\n",
               RT_TICK_PER_SECOND);
    rt_kprintf("FPU context test: A=voluntary B=timer C=mixed D=initial\n");

    a = run_voluntary_test();
    b = run_irq_test();
    c = run_mixed_test();
    d = run_initial_test();

    rt_kprintf("FPU context summary: A=%s B=%s C=%s D=%s\n",
               test_result_name(a), test_result_name(b),
               test_result_name(c), test_result_name(d));

    uassert_int_equal(a, TEST_PASS);
    uassert_int_equal(b, TEST_PASS);
    uassert_int_equal(c, TEST_PASS);
    uassert_int_equal(d, TEST_PASS);
}

UTEST_TC_EXPORT(fpu_context_testcase, "testcases.libcpu.fpu_context",
                RT_NULL, RT_NULL, 60);
