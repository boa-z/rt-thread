/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>

#include "utest.h"
#include "d126_tests.h"

static void tc_tick_advance(void)
{
    rt_uint32_t delta = 0;

    uassert_int_equal(d126_test_tick(100, &delta), 0);
    uassert_true(delta > 0);
}

static void tc_tick_delay_matrix(void)
{
    static const rt_uint32_t delays[] = {1, 2, 5, 10, 50, 200};
    rt_uint32_t delta = 0;
    rt_size_t i;

    for (i = 0; i < sizeof(delays) / sizeof(delays[0]); i++)
    {
        uassert_int_equal(d126_test_tick(delays[i], &delta), 0);
        uassert_true(delta > 0);
    }
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
    UTEST_UNIT_RUN(tc_tick_advance);
    UTEST_UNIT_RUN(tc_tick_delay_matrix);
}
UTEST_TC_EXPORT(testcase, "bsp.d126.tick",
                utest_tc_init, utest_tc_cleanup, 10);
