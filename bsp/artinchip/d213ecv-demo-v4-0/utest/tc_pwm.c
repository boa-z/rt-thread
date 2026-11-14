/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "utest.h"

#ifdef BSP_USING_PWM

#define TC_PWM_CHANNEL 0
#define TC_PWM_PERIOD  1000000U
#define TC_PWM_PULSE   500000U

static void tc_pwm_setup(void)
{
    struct rt_device_pwm *pwm;
    rt_err_t ret;

    pwm = (struct rt_device_pwm *)rt_device_find("pwm");
    uassert_not_null(pwm);
    if (pwm == RT_NULL)
    {
        return;
    }

    ret = rt_pwm_set(pwm, TC_PWM_CHANNEL, TC_PWM_PERIOD, TC_PWM_PULSE);
    uassert_int_equal(ret, RT_EOK);
    if (ret != RT_EOK)
    {
        return;
    }

    ret = rt_pwm_enable(pwm, TC_PWM_CHANNEL);
    uassert_int_equal(ret, RT_EOK);

    /* Always restore the channel to a safe disabled state. */
    uassert_int_equal(rt_pwm_disable(pwm, TC_PWM_CHANNEL), RT_EOK);
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_pwm_setup);
}
UTEST_TC_EXPORT(testcase, "bsp.d213.pwm", RT_NULL, RT_NULL, 10);

#endif /* BSP_USING_PWM */
