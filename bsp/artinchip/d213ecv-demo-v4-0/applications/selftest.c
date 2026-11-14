/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D213 bring-up self test.
 *
 * The FPU context regression is shared with the BSP utest case.  Keep the
 * orchestration here instead of in main.c so that the boot entry stays a
 * plain application entry.
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "selftest.h"

#define SELFTEST_THREAD_STACK   2048
#define SELFTEST_LOOPS          1000
#define SELFTEST_TIMEOUT_MS     5000
#define SELFTEST_SYNC_TIMEOUT   2000

extern rt_uint64_t aic_fpu_context_test(rt_uint64_t seed,
                                        void (*sync)(void *), void *arg);

static struct rt_semaphore counter_done;
static struct rt_semaphore counter_stop;
static struct rt_mutex test_lock;
static volatile rt_uint32_t test_counter;

static struct rt_semaphore fpu_done;
static struct rt_semaphore fpu_sem1;
static struct rt_semaphore fpu_sem2;
static struct rt_semaphore fpu_sem3;
static struct rt_semaphore fpu_stop;
static volatile rt_uint32_t fpu_failed_reg;
static volatile rt_uint32_t fpu_sync_timeout;
static rt_bool_t counter_objects_initialized;
static rt_bool_t fpu_objects_initialized;
static rt_bool_t selftest_running;

/*
 * The FPU test uses a two-thread handshake:
 *   worker0 sets FS registers -> wakes worker1
 *   worker1 sets FS registers -> wakes worker0
 *   worker0 verifies, then wakes worker1, which verifies
 * Both checks therefore happen after the thread has been switched out
 * at least once.  Every wait is bounded so a broken handshake cannot
 * leave the command or the utest thread blocked forever.
 */
static void fpu_sync(void *parameter)
{
    rt_uint32_t id = (rt_uint32_t)(rt_ubase_t)parameter;
    rt_tick_t timeout = rt_tick_from_millisecond(SELFTEST_SYNC_TIMEOUT);

    if (id == 0)
    {
        rt_sem_release(&fpu_sem1);
        if (rt_sem_take(&fpu_sem2, timeout) != RT_EOK)
        {
            fpu_sync_timeout = 1;
        }
    }
    else
    {
        if (rt_sem_take(&fpu_sem1, timeout) != RT_EOK)
        {
            fpu_sync_timeout = 1;
            return;
        }

        rt_sem_release(&fpu_sem2);
        if (rt_sem_take(&fpu_sem3, timeout) != RT_EOK)
        {
            fpu_sync_timeout = 1;
        }
    }
}

static void fpu_worker(void *parameter)
{
    rt_uint32_t id = (rt_uint32_t)(rt_ubase_t)parameter;
    rt_uint64_t seed;
    rt_uint32_t result;

    seed = 0x3ff0000000000000ULL + ((rt_uint64_t)id << 32);
    result = (rt_uint32_t)aic_fpu_context_test(seed, fpu_sync,
                                               (void *)(rt_ubase_t)id);
    if (id == 0)
    {
        /* Let worker1 perform its own post-switch verification. */
        rt_sem_release(&fpu_sem3);
    }

    if (result != 0 && fpu_failed_reg == 0)
    {
        fpu_failed_reg = result;
    }

    /* Keep the worker alive until the caller deletes it. */
    rt_sem_release(&fpu_done);
    rt_sem_take(&fpu_stop, RT_WAITING_FOREVER);
}

rt_err_t aic_fpu_context_check(rt_uint32_t *failed_reg)
{
    rt_thread_t t1 = RT_NULL;
    rt_thread_t t2 = RT_NULL;
    rt_err_t ret = RT_EOK;
    rt_tick_t timeout = rt_tick_from_millisecond(SELFTEST_TIMEOUT_MS);
    int done;

    if (failed_reg != RT_NULL)
    {
        *failed_reg = 0;
    }

    if (fpu_objects_initialized)
    {
        return -RT_EBUSY;
    }

    fpu_failed_reg = 0;
    fpu_sync_timeout = 0;

    rt_sem_init(&fpu_done, "fpu_done", 0, RT_IPC_FLAG_PRIO);
    rt_sem_init(&fpu_sem1, "fpu1", 0, RT_IPC_FLAG_PRIO);
    rt_sem_init(&fpu_sem2, "fpu2", 0, RT_IPC_FLAG_PRIO);
    rt_sem_init(&fpu_sem3, "fpu3", 0, RT_IPC_FLAG_PRIO);
    rt_sem_init(&fpu_stop, "fpu_stop", 0, RT_IPC_FLAG_PRIO);
    fpu_objects_initialized = RT_TRUE;

    t1 = rt_thread_create("fpu0", fpu_worker, (void *)0,
                          SELFTEST_THREAD_STACK, 11, 10);
    t2 = rt_thread_create("fpu1", fpu_worker, (void *)1,
                          SELFTEST_THREAD_STACK, 12, 10);
    if (t1 == RT_NULL || t2 == RT_NULL)
    {
        if (t1 != RT_NULL)
        {
            rt_thread_delete(t1);
        }
        if (t2 != RT_NULL)
        {
            rt_thread_delete(t2);
        }
        ret = -RT_ENOMEM;
        goto cleanup;
    }

    rt_thread_startup(t1);
    rt_thread_startup(t2);

    for (done = 0; done < 2; done++)
    {
        if (rt_sem_take(&fpu_done, timeout) != RT_EOK)
        {
            ret = -RT_ETIMEOUT;
            /*
             * A worker is still alive; leave the IPC objects in place
             * rather than freeing memory it may still reference.  The
             * next invocation reports -RT_EBUSY instead of corrupting
             * the heap.
             */
            return ret;
        }
    }

    if (fpu_sync_timeout)
    {
        ret = -RT_ETIMEOUT;
    }
    else if (fpu_failed_reg != 0)
    {
        ret = -RT_ERROR;
        if (failed_reg != RT_NULL)
        {
            *failed_reg = fpu_failed_reg;
        }
    }

    rt_thread_delete(t1);
    rt_thread_delete(t2);

cleanup:
    rt_sem_detach(&fpu_done);
    rt_sem_detach(&fpu_sem1);
    rt_sem_detach(&fpu_sem2);
    rt_sem_detach(&fpu_sem3);
    rt_sem_detach(&fpu_stop);
    fpu_objects_initialized = RT_FALSE;
    return ret;
}

static void counter_worker(void *parameter)
{
    int i;

    (void)parameter;

    for (i = 0; i < SELFTEST_LOOPS; i++)
    {
        rt_mutex_take(&test_lock, RT_WAITING_FOREVER);
        test_counter++;
        rt_mutex_release(&test_lock);
    }

    /* Keep the worker alive until the caller deletes it. */
    rt_sem_release(&counter_done);
    rt_sem_take(&counter_stop, RT_WAITING_FOREVER);
}

static void aic_selftest(void)
{
    rt_thread_t t1 = RT_NULL;
    rt_thread_t t2 = RT_NULL;
    rt_tick_t start;
    rt_tick_t elapsed;
    rt_tick_t timeout = rt_tick_from_millisecond(SELFTEST_TIMEOUT_MS);
    rt_uint32_t failed_reg = 0;
    rt_err_t fpu_ret;
    int done = 0;

    if (selftest_running)
    {
        rt_kprintf("selftest: already running\n");
        return;
    }
    if (counter_objects_initialized)
    {
        rt_kprintf("selftest: previous worker timeout, reboot required\n");
        return;
    }
    selftest_running = RT_TRUE;

    rt_sem_init(&counter_done, "done", 0, RT_IPC_FLAG_PRIO);
    rt_sem_init(&counter_stop, "stop", 0, RT_IPC_FLAG_PRIO);
    rt_mutex_init(&test_lock, "lock", RT_IPC_FLAG_PRIO);
    counter_objects_initialized = RT_TRUE;
    test_counter = 0;

    t1 = rt_thread_create("worker0", counter_worker, (void *)0,
                          SELFTEST_THREAD_STACK, 11, 10);
    t2 = rt_thread_create("worker1", counter_worker, (void *)1,
                          SELFTEST_THREAD_STACK, 12, 10);
    if (t1 == RT_NULL || t2 == RT_NULL)
    {
        rt_kprintf("selftest: thread create failed\n");
        if (t1 != RT_NULL)
        {
            rt_thread_delete(t1);
        }
        if (t2 != RT_NULL)
        {
            rt_thread_delete(t2);
        }
        goto cleanup;
    }

    start = rt_tick_get();
    rt_thread_startup(t1);
    rt_thread_startup(t2);

    while (done < 2 && rt_sem_take(&counter_done, timeout) == RT_EOK)
    {
        done++;
    }
    elapsed = rt_tick_get() - start;

    if (done != 2)
    {
        /*
         * Do not delete workers or detach IPC objects we can no longer
         * prove are unused.  Leave the flag set so the next call fails
         * cleanly instead of reinitialising live objects.
         */
        rt_kprintf("selftest: worker timeout (%d/2 done)\n", done);
        selftest_running = RT_FALSE;
        return;
    }

    rt_thread_delete(t1);
    rt_thread_delete(t2);
    rt_kprintf("selftest: counter=%u (expected %d), ticks=%u\n",
               test_counter, SELFTEST_LOOPS * 2, (unsigned int)elapsed);

cleanup:
    rt_sem_detach(&counter_done);
    rt_sem_detach(&counter_stop);
    rt_mutex_detach(&test_lock);
    counter_objects_initialized = RT_FALSE;

    if (t1 != RT_NULL || t2 != RT_NULL)
    {
        fpu_ret = aic_fpu_context_check(&failed_reg);
        if (fpu_ret == RT_EOK)
        {
            rt_kprintf("selftest: fpu context OK\n");
        }
        else if (fpu_ret == -RT_ETIMEOUT)
        {
            rt_kprintf("selftest: fpu context TIMEOUT\n");
        }
        else if (fpu_ret == -RT_EBUSY)
        {
            rt_kprintf("selftest: fpu context busy\n");
        }
        else
        {
            rt_kprintf("selftest: fpu context FAIL (fs%u)\n",
                       (unsigned int)failed_reg);
        }
    }

    selftest_running = RT_FALSE;
}
MSH_CMD_EXPORT_ALIAS(aic_selftest, aic_selftest,
                     run D213 bring-up self test);
