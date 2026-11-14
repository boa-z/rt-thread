/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D21x PWM driver.
 *
 * The D21x PWM block has four channels on this board configuration.
 * The vendor HAL (libraries/soc/d21x/hal/hal_pwm.c) is reused for time-base and
 * action-qualifier programming.  Only the RT-Thread PWM device adapter
 * lives here.
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "hal_pwm.h"

static struct rt_device_pwm aic_pwm;

static void aic_pwm_default_action(void)
{
    struct aic_pwm_action action0 =
    {
        .CBD = PWM_ACT_NONE,
        .CBU = PWM_ACT_NONE,
        .CAD = PWM_ACT_NONE,
        .CAU = PWM_ACT_LOW,
        .PRD = PWM_ACT_NONE,
        .ZRO = PWM_ACT_HIGH,
    };
    struct aic_pwm_action action1 =
    {
        .CBD = PWM_ACT_NONE,
        .CBU = PWM_ACT_LOW,
        .CAD = PWM_ACT_NONE,
        .CAU = PWM_ACT_NONE,
        .PRD = PWM_ACT_NONE,
        .ZRO = PWM_ACT_HIGH,
    };
    int ch;

    for (ch = 0; ch < AIC_PWM_CH_NUM; ch++)
        hal_pwm_ch_init(ch, PWM_MODE_UP_COUNT, 0, &action0, &action1);
}

static rt_err_t aic_pwm_control(struct rt_device_pwm *device, int cmd,
                                void *arg)
{
    struct rt_pwm_configuration *cfg = (struct rt_pwm_configuration *)arg;
    int ret;

    (void)device;

    if (cfg == RT_NULL || cfg->channel >= AIC_PWM_CH_NUM)
        return -RT_EINVAL;

    switch (cmd)
    {
    case PWM_CMD_ENABLE:
        ret = hal_pwm_enable(cfg->channel);
        return (ret == 0) ? RT_EOK : -RT_EIO;

    case PWM_CMD_DISABLE:
        ret = hal_pwm_disable(cfg->channel);
        return (ret == 0) ? RT_EOK : -RT_EIO;

    case PWM_CMD_SET:
        if (cfg->pulse > cfg->period || cfg->period == 0)
            return -RT_EINVAL;
        ret = hal_pwm_set(cfg->channel, cfg->pulse, cfg->period,
                          PWM_SET_CMPA_CMPB);
        return (ret == 0) ? RT_EOK : -RT_EIO;

    case PWM_CMD_GET:
        ret = hal_pwm_get(cfg->channel, &cfg->pulse, &cfg->period);
        return (ret == 0) ? RT_EOK : -RT_EIO;

    default:
        return -RT_EINVAL;
    }
}

static const struct rt_pwm_ops aic_pwm_ops =
{
    .control = aic_pwm_control,
};

int rt_hw_pwm_init(void)
{
    if (hal_pwm_init() != 0)
    {
        rt_kprintf("pwm: HAL init failed\n");
        return -RT_ERROR;
    }

    aic_pwm_default_action();

    return rt_device_pwm_register(&aic_pwm, "pwm", &aic_pwm_ops, RT_NULL);
}
INIT_DEVICE_EXPORT(rt_hw_pwm_init);
