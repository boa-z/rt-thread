/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>

#include "utest.h"

#define TC_TICK_DELAY_MS      120
#define TC_TIMER_PERIOD_MS    20
#define TC_TIMER_TIMEOUT_MS   1000

static struct rt_semaphore tc_timer_sem;

static void tc_timer_callback(void *parameter)
{
    (void)parameter;
    rt_sem_release(&tc_timer_sem);
}

static void tc_tick_advance(void)
{
    rt_tick_t start;
    rt_tick_t delta;
    rt_tick_t lower = rt_tick_from_millisecond(50);
    rt_tick_t upper = rt_tick_from_millisecond(2000);

    start = rt_tick_get();
    rt_thread_mdelay(TC_TICK_DELAY_MS);
    delta = rt_tick_get() - start;

    uassert_true(delta >= lower);
    uassert_true(delta <= upper);
}

static void tc_timer_oneshot(void)
{
    rt_timer_t timer;
    rt_err_t ret;

    timer = rt_timer_create("tc_tick", tc_timer_callback, RT_NULL,
                            rt_tick_from_millisecond(TC_TIMER_PERIOD_MS),
                            RT_TIMER_FLAG_ONE_SHOT);
    uassert_not_null(timer);
    if (timer == RT_NULL)
    {
        return;
    }

    ret = rt_timer_start(timer);
    uassert_int_equal(ret, RT_EOK);
    if (ret == RT_EOK)
    {
        ret = rt_sem_take(&tc_timer_sem,
                          rt_tick_from_millisecond(TC_TIMER_TIMEOUT_MS));
        uassert_int_equal(ret, RT_EOK);
    }

    uassert_int_equal(rt_timer_delete(timer), RT_EOK);
}

static rt_err_t utest_tc_init(void)
{
    rt_sem_init(&tc_timer_sem, "tc_tick", 0, RT_IPC_FLAG_FIFO);
    return RT_EOK;
}

static rt_err_t utest_tc_cleanup(void)
{
    rt_sem_detach(&tc_timer_sem);
    return RT_EOK;
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_tick_advance);
    UTEST_UNIT_RUN(tc_timer_oneshot);
}
UTEST_TC_EXPORT(testcase, "bsp.d213.tick", utest_tc_init, utest_tc_cleanup, 10);
