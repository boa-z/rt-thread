/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>

#include "utest.h"
#include "selftest.h"

static void tc_fpu_context(void)
{
    rt_uint32_t failed_reg = 0;
    rt_err_t ret;

    ret = aic_fpu_context_check(&failed_reg);

    if (ret == -RT_ERROR)
    {
        rt_kprintf("fpu context: fs%u was not preserved across the switch\n",
                   (unsigned int)failed_reg);
        if (failed_reg == 2)
        {
            rt_kprintf("fpu context: fs1 is the known pure-BSP common-port "
                       "limitation; use integration/artinchip-fpu-test\n");
        }
    }
    else if (ret == -RT_ETIMEOUT)
    {
        rt_kprintf("fpu context: worker handshake timed out\n");
    }

    uassert_int_equal(ret, RT_EOK);
    uassert_int_equal(failed_reg, 0);
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_fpu_context);
}
UTEST_TC_EXPORT(testcase, "bsp.d213.fpu.context", RT_NULL, RT_NULL, 20);
