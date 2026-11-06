/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>

#include "utest.h"

#define TC_CONTEXT_DELAY_MS 20

extern rt_uint64_t aic_integer_context_test(rt_uint64_t seed,
                                            void (*sync)(void *), void *arg);

static void tc_context_sync(void *parameter)
{
    volatile rt_uint32_t *switched = (volatile rt_uint32_t *)parameter;

    rt_thread_mdelay(TC_CONTEXT_DELAY_MS);
    *switched = 1;
}

static void tc_integer_context(void)
{
    volatile rt_uint32_t switched = 0;
    rt_uint64_t result;

    result = aic_integer_context_test(0x1122334455660000ULL,
                                      tc_context_sync,
                                      (void *)&switched);

    uassert_int_equal(switched, 1);
    uassert_int_equal((rt_uint32_t)result, 0);

    if (result != 0)
    {
        rt_kprintf("integer context: first mismatch s%u\n",
                   (unsigned int)(result - 1));
    }
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_integer_context);
}
UTEST_TC_EXPORT(testcase, "bsp.d213.context", RT_NULL, RT_NULL, 10);
