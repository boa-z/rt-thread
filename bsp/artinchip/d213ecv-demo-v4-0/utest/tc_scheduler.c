/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>

#include "utest.h"

#define TC_SCHED_STACK      2048
#define TC_SCHED_LOOPS      1000
#define TC_SCHED_TIMEOUT_MS 5000

static struct rt_semaphore tc_sched_done;
static struct rt_semaphore tc_sched_stop;
static struct rt_mutex tc_sched_lock;
static volatile rt_uint32_t tc_sched_counter;
static volatile rt_uint32_t tc_sched_exited;

static void tc_sched_exit_worker(void *parameter)
{
    (void)parameter;
    tc_sched_exited = 1;
}

static void tc_sched_thread_exit(void)
{
    rt_thread_t thread;
    int wait;

    tc_sched_exited = 0;
    thread = rt_thread_create("ut_exit", tc_sched_exit_worker, RT_NULL,
                              1024, 12, 10);
    uassert_not_null(thread);
    if (thread == RT_NULL)
    {
        return;
    }

    rt_thread_startup(thread);
    for (wait = 0; wait < 100 && !tc_sched_exited; wait++)
    {
        rt_thread_mdelay(10);
    }

    uassert_int_equal(tc_sched_exited, 1);

    /* Give the idle thread time to reap the dynamic thread object. */
    for (wait = 0; wait < 100 && rt_thread_find("ut_exit") != RT_NULL; wait++)
    {
        rt_thread_mdelay(10);
    }
    uassert_null(rt_thread_find("ut_exit"));
}

static void tc_sched_worker(void *parameter)
{
    int i;

    (void)parameter;

    for (i = 0; i < TC_SCHED_LOOPS; i++)
    {
        rt_mutex_take(&tc_sched_lock, RT_WAITING_FOREVER);
        tc_sched_counter++;
        rt_mutex_release(&tc_sched_lock);
    }

    rt_sem_release(&tc_sched_done);
    rt_sem_take(&tc_sched_stop, RT_WAITING_FOREVER);
}

static void tc_scheduler_threads(void)
{
    rt_thread_t t1;
    rt_thread_t t2;
    rt_tick_t timeout = rt_tick_from_millisecond(TC_SCHED_TIMEOUT_MS);
    int done = 0;

    tc_sched_counter = 0;
    t1 = rt_thread_create("ut_work0", tc_sched_worker, RT_NULL,
                          TC_SCHED_STACK, 11, 10);
    t2 = rt_thread_create("ut_work1", tc_sched_worker, RT_NULL,
                          TC_SCHED_STACK, 12, 10);
    uassert_not_null(t1);
    uassert_not_null(t2);

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
        return;
    }

    rt_thread_startup(t1);
    rt_thread_startup(t2);

    while (done < 2 && rt_sem_take(&tc_sched_done, timeout) == RT_EOK)
    {
        done++;
    }

    uassert_int_equal(done, 2);
    uassert_int_equal(tc_sched_counter, TC_SCHED_LOOPS * 2);

    rt_thread_delete(t1);
    rt_thread_delete(t2);
}

static void tc_semaphore_timeout(void)
{
    struct rt_semaphore sem;
    rt_err_t ret;

    uassert_int_equal(rt_sem_init(&sem, "ut_sem", 0, RT_IPC_FLAG_FIFO),
                      RT_EOK);

    ret = rt_sem_take(&sem, rt_tick_from_millisecond(20));
    uassert_int_equal(ret, -RT_ETIMEOUT);

    uassert_int_equal(rt_sem_release(&sem), RT_EOK);
    uassert_int_equal(rt_sem_take(&sem, 0), RT_EOK);

    uassert_int_equal(rt_sem_detach(&sem), RT_EOK);
}

static rt_err_t utest_tc_init(void)
{
    rt_sem_init(&tc_sched_done, "ut_done", 0, RT_IPC_FLAG_PRIO);
    rt_sem_init(&tc_sched_stop, "ut_stop", 0, RT_IPC_FLAG_PRIO);
    rt_mutex_init(&tc_sched_lock, "ut_lock", RT_IPC_FLAG_PRIO);
    return RT_EOK;
}

static rt_err_t utest_tc_cleanup(void)
{
    rt_mutex_detach(&tc_sched_lock);
    rt_sem_detach(&tc_sched_stop);
    rt_sem_detach(&tc_sched_done);
    return RT_EOK;
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_scheduler_threads);
    UTEST_UNIT_RUN(tc_semaphore_timeout);
    UTEST_UNIT_RUN(tc_sched_thread_exit);
}
UTEST_TC_EXPORT(testcase, "bsp.d213.scheduler",
                utest_tc_init, utest_tc_cleanup, 20);
