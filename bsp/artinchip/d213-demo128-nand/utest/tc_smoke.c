/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "utest.h"

static void tc_smoke_basics(void)
{
    void *buffer;

    uassert_not_null(utest_handle_get());
    uassert_not_null(rt_thread_self());

    buffer = rt_malloc(64);
    uassert_not_null(buffer);
    if (buffer != RT_NULL)
    {
        rt_memset(buffer, 0x5a, 64);
        uassert_int_equal(((rt_uint8_t *)buffer)[0], 0x5a);
        uassert_int_equal(((rt_uint8_t *)buffer)[63], 0x5a);
        rt_free(buffer);
    }
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_smoke_basics);
}
UTEST_TC_EXPORT(testcase, "bsp.d213.smoke", RT_NULL, RT_NULL, 10);
