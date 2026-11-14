/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * D213ECV-DEMO-V4-0 key ladder driver.
 *
 * K4..K7 (UP/DOWN/LEFT/RIGHT) share PA.6/KEYADC.  The board has a 10 k
 * pull-up to VCC3V3 (R157) and one resistor per key to GND:
 *
 *   K4 UP    R153 1.0 k
 *   K5 DOWN  R154 2.2 k
 *   K6 LEFT  R155 4.7 k
 *   K7 RIGHT R156 6.8 k
 *
 * Released, the input is pulled to 3.3 V and the 12-bit ADC saturates.
 * The driver samples GPAI channel 6 every 10 ms, debounces the decoded
 * key for three samples and reports press/release/long-press events.
 * This is a polling design because the ladder has no GPIO interrupt.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <drivers/adc.h>

#include "drv_key.h"

#define KEY_ADC_DEVICE          "gpai"
#define KEY_ADC_CHANNEL         6
#define KEY_ADC_MAX_VAL         0xFFFU
#define KEY_ADC_FULL_SCALE      4095U

/* Board ladder: VCC3V3, 10 k pull-up, key resistor to GND. */
#define KEY_LADDER_VCC_MV       3300U
#define KEY_LADDER_PULLUP_OHM   10000U

/*
 * Expected codes are more than 300 codes apart, so 150 codes of
 * tolerance absorbs the 5 % rail and resistor variation without
 * reaching the neighbouring key.
 */
#define KEY_ADC_TOLERANCE       150U
#define KEY_ADC_RELEASE_RAW     (KEY_ADC_FULL_SCALE - KEY_ADC_TOLERANCE)

#define KEY_POLL_MS             10
#define KEY_DEBOUNCE_COUNT      3
#define KEY_THREAD_STACK        1024
#define KEY_THREAD_PRIORITY     20
#define KEY_MAX_CALLBACKS       4

#ifndef BSP_KEYADC_LONG_PRESS_MS
#define BSP_KEYADC_LONG_PRESS_MS 1000
#endif

struct aic_key_ladder
{
    enum aic_key_id key;
    rt_uint32_t r_ohm;
};

static const struct aic_key_ladder key_ladder[] =
{
    { AIC_KEY_UP,    1000 },
    { AIC_KEY_DOWN,  2200 },
    { AIC_KEY_LEFT,  4700 },
    { AIC_KEY_RIGHT, 6800 },
};

#define KEY_LADDER_COUNT    (sizeof(key_ladder) / sizeof(key_ladder[0]))

static aic_key_event_cb key_callbacks[KEY_MAX_CALLBACKS];
static rt_uint32_t key_raw;
static rt_uint16_t key_vref_mv;
static rt_uint32_t key_press_count;
static rt_bool_t key_monitor;

static rt_uint32_t key_expected_raw(rt_uint32_t r_ohm, rt_uint16_t vref_mv)
{
    rt_uint64_t num;
    rt_uint64_t den;

    /* VCC * R / (R + Rpull), in mV, then scaled by the ADC reference. */
    num = (rt_uint64_t)KEY_LADDER_VCC_MV * r_ohm * KEY_ADC_FULL_SCALE;
    den = (rt_uint64_t)(r_ohm + KEY_LADDER_PULLUP_OHM) * vref_mv;

    return (rt_uint32_t)((num + den / 2) / den);
}

enum aic_key_id aic_keyadc_decode(rt_uint32_t raw, rt_uint16_t vref_mv)
{
    rt_uint32_t best_diff = KEY_ADC_TOLERANCE + 1;
    enum aic_key_id best = AIC_KEY_NONE;
    rt_size_t i;

    raw &= KEY_ADC_MAX_VAL;
    if (vref_mv == 0)
        vref_mv = 3000;

    /* Released: the 3.3 V pull-up drives the input to full scale. */
    if (raw >= KEY_ADC_RELEASE_RAW)
        return AIC_KEY_NONE;

    for (i = 0; i < KEY_LADDER_COUNT; i++)
    {
        rt_uint32_t expected;
        rt_uint32_t diff;

        expected = key_expected_raw(key_ladder[i].r_ohm, vref_mv);
        diff = raw > expected ? raw - expected : expected - raw;
        if (diff <= KEY_ADC_TOLERANCE && diff < best_diff)
        {
            best_diff = diff;
            best = key_ladder[i].key;
        }
    }

    return best;
}

static const char *key_name(enum aic_key_id key)
{
    switch (key)
    {
    case AIC_KEY_UP:
        return "UP";
    case AIC_KEY_DOWN:
        return "DOWN";
    case AIC_KEY_LEFT:
        return "LEFT";
    case AIC_KEY_RIGHT:
        return "RIGHT";
    default:
        return "NONE";
    }
}

rt_uint32_t aic_keyadc_raw(void)
{
    return key_raw;
}

rt_err_t aic_key_register_callback(aic_key_event_cb cb)
{
    rt_size_t i;
    rt_base_t level;

    if (!cb)
        return -RT_EINVAL;

    level = rt_hw_interrupt_disable();
    for (i = 0; i < KEY_MAX_CALLBACKS; i++)
    {
        if (!key_callbacks[i])
        {
            key_callbacks[i] = cb;
            rt_hw_interrupt_enable(level);
            return RT_EOK;
        }
    }
    rt_hw_interrupt_enable(level);

    return -RT_EFULL;
}

rt_err_t aic_key_unregister_callback(aic_key_event_cb cb)
{
    rt_size_t i;
    rt_base_t level;

    if (!cb)
        return -RT_EINVAL;

    level = rt_hw_interrupt_disable();
    for (i = 0; i < KEY_MAX_CALLBACKS; i++)
    {
        if (key_callbacks[i] == cb)
        {
            key_callbacks[i] = RT_NULL;
            rt_hw_interrupt_enable(level);
            return RT_EOK;
        }
    }
    rt_hw_interrupt_enable(level);

    return -RT_ENOENT;
}

static void key_report(enum aic_key_id key, rt_bool_t pressed,
                       rt_bool_t long_press)
{
    struct aic_key_event event;
    rt_size_t i;

    event.key = key;
    event.pressed = pressed;
    event.long_press = long_press;

    if (key_monitor)
    {
        rt_kprintf("key: %s %s%s\n", key_name(key),
                   long_press ? "long " : "",
                   pressed ? "pressed" : "released");
    }

    for (i = 0; i < KEY_MAX_CALLBACKS; i++)
    {
        if (key_callbacks[i])
            key_callbacks[i](&event);
    }
}

static void key_thread_entry(void *parameter)
{
    rt_adc_device_t adc;
    enum aic_key_id current = AIC_KEY_NONE;
    enum aic_key_id last_sample = AIC_KEY_NONE;
    rt_uint32_t stable = 0;
    rt_tick_t press_tick = 0;
    rt_bool_t long_sent = RT_FALSE;
    rt_tick_t long_ticks;

    adc = (rt_adc_device_t)rt_device_find(KEY_ADC_DEVICE);
    if (!adc)
    {
        rt_kprintf("key: %s ADC device not found\n", KEY_ADC_DEVICE);
        return;
    }

    if (rt_adc_enable(adc, KEY_ADC_CHANNEL) != RT_EOK)
    {
        rt_kprintf("key: cannot enable ADC channel %d\n", KEY_ADC_CHANNEL);
        return;
    }

    key_vref_mv = 0;
    rt_device_control((rt_device_t)adc, RT_ADC_CMD_GET_VREF, &key_vref_mv);
    if (key_vref_mv == 0)
        key_vref_mv = 3000;

    long_ticks = rt_tick_from_millisecond(BSP_KEYADC_LONG_PRESS_MS);

    for (;;)
    {
        enum aic_key_id sample;

        key_raw = rt_adc_read(adc, KEY_ADC_CHANNEL) & KEY_ADC_MAX_VAL;
        sample = aic_keyadc_decode(key_raw, key_vref_mv);

        if (sample == last_sample)
        {
            if (stable < KEY_DEBOUNCE_COUNT)
                stable++;
        }
        else
        {
            last_sample = sample;
            stable = 0;
        }

        if (stable >= KEY_DEBOUNCE_COUNT && sample != current)
        {
            if (current != AIC_KEY_NONE)
                key_report(current, RT_FALSE, RT_FALSE);

            if (sample != AIC_KEY_NONE)
            {
                key_press_count++;
                press_tick = rt_tick_get();
                long_sent = RT_FALSE;
                key_report(sample, RT_TRUE, RT_FALSE);
            }

            current = sample;
        }

        if (current != AIC_KEY_NONE && !long_sent &&
            (rt_tick_get() - press_tick) >= long_ticks)
        {
            long_sent = RT_TRUE;
            key_report(current, RT_TRUE, RT_TRUE);
        }

        rt_thread_mdelay(KEY_POLL_MS);
    }
}

static int drv_key_init(void)
{
    rt_thread_t tid;

    tid = rt_thread_create("keyadc", key_thread_entry, RT_NULL,
                           KEY_THREAD_STACK, KEY_THREAD_PRIORITY, KEY_POLL_MS);
    if (!tid)
    {
        rt_kprintf("key: cannot create key thread\n");
        return -RT_ERROR;
    }

    rt_thread_startup(tid);

    return RT_EOK;
}
INIT_APP_EXPORT(drv_key_init);

static void keyadc(int argc, char **argv)
{
    if (argc > 1 && !rt_strcmp(argv[1], "monitor"))
    {
        key_monitor = !key_monitor;
        rt_kprintf("key: monitor %s\n", key_monitor ? "on" : "off");
        return;
    }

    rt_kprintf("key: raw %u, vref %u mV, presses %u, monitor %s\n",
               key_raw, key_vref_mv, key_press_count,
               key_monitor ? "on" : "off");
    rt_kprintf("key: usage: keyadc [monitor]\n");
}
MSH_CMD_EXPORT(keyadc, D213 KEYADC status or event monitor);
