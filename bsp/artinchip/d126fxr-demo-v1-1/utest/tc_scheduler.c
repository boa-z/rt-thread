/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>

#include "utest.h"
#include "d126_tests.h"

#define PREEMPT_PRIO_HIGH   8
#define PREEMPT_PRIO_LOW    24
#define PREEMPT_STACK       1024
#define PREEMPT_TIMEOUT_MS  500

static struct rt_semaphore preempt_start;
static struct rt_semaphore preempt_done;
static volatile rt_uint32_t preempt_order;
static volatile rt_bool_t preempt_ok;

static void preempt_high_thread(void *parameter)
{
    (void)parameter;

    rt_sem_take(&preempt_start, RT_WAITING_FOREVER);
    if (preempt_order != 1)
    {
        preempt_ok = RT_FALSE;
    }
    preempt_order = 2;
    rt_sem_release(&preempt_done);
}

static void preempt_low_thread(void *parameter)
{
    (void)parameter;

    preempt_order = 1;
    rt_sem_release(&preempt_start);
    /*
     * The woken high-priority thread must run before this point; if it
     * did not, order is still 1 and the test fails.
     */
    if (preempt_order != 2)
    {
        preempt_ok = RT_FALSE;
    }
    preempt_order = 3;
    rt_sem_release(&preempt_done);
}

static void tc_scheduler_preempt(void)
{
    rt_thread_t high;
    rt_thread_t low;
    rt_tick_t timeout = rt_tick_from_millisecond(PREEMPT_TIMEOUT_MS);
    int done = 0;

    preempt_order = 0;
    preempt_ok = RT_TRUE;

    if (rt_sem_init(&preempt_start, "preempt0", 0, RT_IPC_FLAG_PRIO) != RT_EOK)
    {
        uassert_true(RT_FALSE);
        return;
    }
    if (rt_sem_init(&preempt_done, "preempt1", 0, RT_IPC_FLAG_PRIO) != RT_EOK)
    {
        uassert_true(RT_FALSE);
        rt_sem_detach(&preempt_start);
        return;
    }

    high = rt_thread_create("preempt-hi", preempt_high_thread, RT_NULL,
                            PREEMPT_STACK, PREEMPT_PRIO_HIGH, 10);
    low = rt_thread_create("preempt-lo", preempt_low_thread, RT_NULL,
                           PREEMPT_STACK, PREEMPT_PRIO_LOW, 10);
    uassert_not_null(high);
    uassert_not_null(low);
    if (high == RT_NULL || low == RT_NULL)
    {
        if (high != RT_NULL)
        {
            rt_thread_delete(high);
        }
        if (low != RT_NULL)
        {
            rt_thread_delete(low);
        }
        rt_sem_detach(&preempt_done);
        rt_sem_detach(&preempt_start);
        return;
    }

    rt_thread_startup(high);
    rt_thread_startup(low);

    while (done < 2 && rt_sem_take(&preempt_done, timeout) == RT_EOK)
    {
        done++;
    }

    uassert_int_equal(done, 2);
    uassert_true(preempt_ok);
    uassert_int_equal(preempt_order, 3);

    /* Let the idle thread reap both dynamic threads before detaching. */
    rt_thread_mdelay(20);
    rt_sem_detach(&preempt_done);
    rt_sem_detach(&preempt_start);
}

static void tc_scheduler_threads(void)
{
    rt_uint32_t counter = 0;
    rt_uint32_t loops = 200;

    uassert_int_equal(d126_test_sched(loops, &counter), 0);
    uassert_int_equal(counter, loops * 2);
}

static rt_err_t utest_tc_init(void)
{
    return RT_EOK;
}

static rt_err_t utest_tc_cleanup(void)
{
    return RT_EOK;
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_scheduler_threads);
    UTEST_UNIT_RUN(tc_scheduler_preempt);
}
UTEST_TC_EXPORT(testcase, "bsp.d126.scheduler",
                utest_tc_init, utest_tc_cleanup, 30);
