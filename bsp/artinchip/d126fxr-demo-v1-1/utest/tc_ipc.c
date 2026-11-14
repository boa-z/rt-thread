/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>

#include "utest.h"
#include "d126_tests.h"

static void tc_basic_ipc(void)
{
    uassert_int_equal(d126_test_ipc(), 0);
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
    UTEST_UNIT_RUN(tc_basic_ipc);
}
UTEST_TC_EXPORT(testcase, "bsp.d126.ipc",
                utest_tc_init, utest_tc_cleanup, 10);
