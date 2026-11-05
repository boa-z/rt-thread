/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D213 minimal BSP entry and bring-up self test.
 */

#include <rtthread.h>
#include <rtdevice.h>

#define SELFTEST_THREAD_STACK 2048
#define SELFTEST_LOOPS        1000

extern rt_uint64_t aic_fpu_context_test(rt_uint64_t seed,
                                        void (*sync)(void *), void *arg);

static struct rt_semaphore test_done;
static struct rt_mutex test_lock;
static volatile rt_uint32_t test_counter;
static rt_bool_t selftest_running;

/*
 * The FPU test uses a three-phase handshake:
 *   worker0 sets FS registers -> wakes worker1
 *   worker1 sets FS registers -> wakes worker0
 *   worker0 verifies, then wakes worker1, which verifies
 * Both checks therefore happen after the thread has been switched out
 * at least once.
 */
static struct rt_semaphore fpu_sem1;
static struct rt_semaphore fpu_sem2;
static struct rt_semaphore fpu_sem3;

static void fpu_sync(void *parameter)
{
    rt_uint32_t id = (rt_uint32_t)(rt_ubase_t)parameter;

    if (id == 0)
    {
        rt_sem_release(&fpu_sem1);
        rt_sem_take(&fpu_sem2, RT_WAITING_FOREVER);
    }
    else
    {
        rt_sem_take(&fpu_sem1, RT_WAITING_FOREVER);
        rt_sem_release(&fpu_sem2);
        rt_sem_take(&fpu_sem3, RT_WAITING_FOREVER);
    }
}

static void selftest_worker(void *parameter)
{
    rt_uint32_t id = (rt_uint32_t)(rt_ubase_t)parameter;
    rt_uint64_t seed;
    rt_uint32_t fpu_result;
    int i;

    for (i = 0; i < SELFTEST_LOOPS; i++)
    {
        rt_mutex_take(&test_lock, RT_WAITING_FOREVER);
        test_counter++;
        rt_mutex_release(&test_lock);
    }

    seed = 0x3ff0000000000000ULL + ((rt_uint64_t)id << 32);
    fpu_result = (rt_uint32_t)aic_fpu_context_test(seed, fpu_sync,
                                                   (void *)(rt_ubase_t)id);

    if (id == 0)
    {
        rt_sem_release(&fpu_sem3);
    }

    if (fpu_result == 0)
    {
        rt_kprintf("worker%u: fpu context OK\n", id);
    }
    else
    {
        rt_kprintf("worker%u: fpu context FAIL (fs%u)\n", id, fpu_result);
    }

    rt_sem_release(&test_done);
}

static void aic_selftest(void)
{
    rt_thread_t t1;
    rt_thread_t t2;
    rt_tick_t start;
    rt_tick_t elapsed;

    if (selftest_running)
    {
        rt_kprintf("selftest: already running\n");
        return;
    }
    selftest_running = RT_TRUE;

    rt_sem_init(&test_done, "done", 0, RT_IPC_FLAG_PRIO);
    rt_mutex_init(&test_lock, "lock", RT_IPC_FLAG_PRIO);
    rt_sem_init(&fpu_sem1, "fpu1", 0, RT_IPC_FLAG_PRIO);
    rt_sem_init(&fpu_sem2, "fpu2", 0, RT_IPC_FLAG_PRIO);
    rt_sem_init(&fpu_sem3, "fpu3", 0, RT_IPC_FLAG_PRIO);
    test_counter = 0;

    /*
     * fpu_sync() implements a two-thread handshake: id 0 releases fpu_sem1
     * and waits on fpu_sem2, id 1 waits on fpu_sem1 and then releases
     * fpu_sem2/fpu_sem3.  The IDs must start at 0; using 1/2 makes both
     * workers wait on fpu_sem1 and deadlocks the self test.
     */
    t1 = rt_thread_create("worker0", selftest_worker, (void *)0,
                          SELFTEST_THREAD_STACK, 11, 10);
    t2 = rt_thread_create("worker1", selftest_worker, (void *)1,
                          SELFTEST_THREAD_STACK, 12, 10);
    if (t1 == RT_NULL || t2 == RT_NULL)
    {
        rt_kprintf("selftest: thread create failed\n");
        rt_sem_detach(&test_done);
        rt_mutex_detach(&test_lock);
        rt_sem_detach(&fpu_sem1);
        rt_sem_detach(&fpu_sem2);
        rt_sem_detach(&fpu_sem3);
        selftest_running = RT_FALSE;
        return;
    }

    start = rt_tick_get();
    rt_thread_startup(t1);
    rt_thread_startup(t2);

    rt_sem_take(&test_done, RT_WAITING_FOREVER);
    rt_sem_take(&test_done, RT_WAITING_FOREVER);
    elapsed = rt_tick_get() - start;

    rt_kprintf("selftest: counter=%u (expected %d), ticks=%u\n",
               test_counter, SELFTEST_LOOPS * 2, (unsigned int)elapsed);

    /*
     * The command can be invoked repeatedly from MSH.  The objects are
     * static, so detach them before the next invocation; otherwise the
     * RT-Thread object container rejects a second rt_sem_init().
     * Both workers have already released test_done as their last action.
     */
    rt_sem_detach(&test_done);
    rt_mutex_detach(&test_lock);
    rt_sem_detach(&fpu_sem1);
    rt_sem_detach(&fpu_sem2);
    rt_sem_detach(&fpu_sem3);
    selftest_running = RT_FALSE;
}
MSH_CMD_EXPORT_ALIAS(aic_selftest, aic_selftest, run D213 bring up self test);

int main(void)
{
    rt_kprintf("\n");
    rt_kprintf("ArtInChip D213 demo128 RT-Thread BSP\n");
    rt_kprintf("CPU: T-Head C906 (RV64GC), tick: %d Hz\n", RT_TICK_PER_SECOND);

    aic_selftest();

    rt_kprintf("main: running\n");
    return 0;
}
