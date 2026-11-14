/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * D213ECV-DEMO-V4-0 key ladder API.
 *
 * The board routes K4..K7 (UP/DOWN/LEFT/RIGHT) to one resistor ladder on
 * PA.6/KEYADC.  drv_key.c samples GPAI channel 6, debounces the result
 * and reports press/release events to registered callbacks.  The decode
 * function is pure so it can also be exercised by the utest suite.
 */

#ifndef __DRV_KEY_H__
#define __DRV_KEY_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

enum aic_key_id
{
    AIC_KEY_NONE = 0,
    AIC_KEY_UP,
    AIC_KEY_DOWN,
    AIC_KEY_LEFT,
    AIC_KEY_RIGHT,
};

struct aic_key_event
{
    enum aic_key_id key;
    rt_bool_t pressed;
    rt_bool_t long_press;
};

typedef void (*aic_key_event_cb)(const struct aic_key_event *event);

/*
 * Map one 12-bit GPAI sample to a key.  vref_mv is the ADC reference
 * reported by RT_ADC_CMD_GET_VREF; the board's ladder is ratiometric to
 * the 3.3 V rail, so the expected code depends on both.
 */
enum aic_key_id aic_keyadc_decode(rt_uint32_t raw, rt_uint16_t vref_mv);

/* Latest raw sample (0 when the ADC has not produced a sample yet). */
rt_uint32_t aic_keyadc_raw(void);

/* Register/unregister an event callback (called from the key thread). */
rt_err_t aic_key_register_callback(aic_key_event_cb cb);
rt_err_t aic_key_unregister_callback(aic_key_event_cb cb);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_KEY_H__ */
