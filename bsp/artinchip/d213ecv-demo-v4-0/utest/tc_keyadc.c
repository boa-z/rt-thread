/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <drivers/adc.h>

#include "utest.h"
#include "drv_key.h"

#ifdef BSP_USING_KEYADC

#define TC_KEYADC_TOLERANCE     150U

static const struct
{
    enum aic_key_id key;
    rt_uint32_t r_ohm;
} tc_key_ladder[] =
{
    { AIC_KEY_UP,    1000 },
    { AIC_KEY_DOWN,  2200 },
    { AIC_KEY_LEFT,  4700 },
    { AIC_KEY_RIGHT, 6800 },
};

static rt_uint32_t tc_expected_raw(rt_uint32_t r_ohm, rt_uint16_t vref_mv)
{
    rt_uint64_t num = (rt_uint64_t)3300U * r_ohm * 4095U;
    rt_uint64_t den = (rt_uint64_t)(r_ohm + 10000U) * vref_mv;

    return (rt_uint32_t)((num + den / 2) / den);
}

static void tc_keyadc_decode(void)
{
    static const rt_uint16_t vrefs[] = { 2800, 3000, 3150 };
    rt_size_t i, j;

    for (i = 0; i < sizeof(vrefs) / sizeof(vrefs[0]); i++)
    {
        rt_uint16_t vref = vrefs[i];

        for (j = 0; j < sizeof(tc_key_ladder) / sizeof(tc_key_ladder[0]); j++)
        {
            rt_uint32_t expected;

            expected = tc_expected_raw(tc_key_ladder[j].r_ohm, vref);
            uassert_int_equal(aic_keyadc_decode(expected, vref),
                              tc_key_ladder[j].key);
            uassert_int_equal(
                aic_keyadc_decode(expected - TC_KEYADC_TOLERANCE, vref),
                tc_key_ladder[j].key);
            uassert_int_equal(
                aic_keyadc_decode(expected + TC_KEYADC_TOLERANCE, vref),
                tc_key_ladder[j].key);
        }

        /* Released input is pulled to the 3.3 V rail and saturates. */
        uassert_int_equal(aic_keyadc_decode(4095, vref), AIC_KEY_NONE);
        uassert_int_equal(aic_keyadc_decode(3945, vref), AIC_KEY_NONE);
        /* No key between two ladder steps, and an ADC read failure. */
        uassert_int_equal(aic_keyadc_decode(1024, vref), AIC_KEY_NONE);
        uassert_int_equal(aic_keyadc_decode(0, vref), AIC_KEY_NONE);
    }
}

static void tc_keyadc_device(void)
{
    rt_adc_device_t adc;
    rt_uint8_t resolution = 0;
    rt_int16_t vref = 0;
    rt_err_t ret;

    adc = (rt_adc_device_t)rt_device_find("gpai");
    uassert_not_null(adc);
    if (adc == RT_NULL)
        return;

    ret = rt_device_control((rt_device_t)adc, RT_ADC_CMD_GET_RESOLUTION,
                            &resolution);
    uassert_int_equal(ret, RT_EOK);
    uassert_int_equal(resolution, 12);

    ret = rt_device_control((rt_device_t)adc, RT_ADC_CMD_GET_VREF, &vref);
    uassert_int_equal(ret, RT_EOK);
    uassert_true(vref >= 2800);
    uassert_true(vref <= 3150);

    rt_kprintf("keyadc: gpai %u-bit, vref %d mV\n", resolution, vref);
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_keyadc_decode);
    UTEST_UNIT_RUN(tc_keyadc_device);
}
UTEST_TC_EXPORT(testcase, "bsp.d213.keyadc", RT_NULL, RT_NULL, 10);

#endif /* BSP_USING_KEYADC */
