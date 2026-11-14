/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Audio playback (DSPK) device test.
 *
 * This proves the software path and the hardware clock/DRQ behaviour:
 * the codec clock tree is programmed, the codec accepts the format, the
 * TX FIFO raises DRQ and the cyclic DMAC completes periods.  It cannot
 * prove acoustic output: that needs a listener or a microphone.
 *
 * The test plays ~43 ms of a 1 kHz square wave at a low level.  The
 * amplifier is enabled by the driver for that interval and shut down
 * again when the device is closed.
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "utest.h"
#include "board.h"
#include "drv_audio.h"

#ifdef BSP_USING_AUDIO

#define TC_AUDIO_RATE       48000
#define TC_AUDIO_CHANNELS   2
/*
 * Two replay memory-pool blocks: the framework only queues a block once
 * it is full, so a shorter write would not start real playback.
 */
#define TC_AUDIO_BYTES      8192

static rt_uint8_t tc_audio_buf[TC_AUDIO_BYTES]
    __attribute__((aligned(64)));

static void tc_audio_fill(rt_uint8_t *buf, rt_size_t len, rt_uint32_t phase0)
{
    rt_int16_t *p = (rt_int16_t *)buf;
    rt_size_t frames = len / 4;
    rt_size_t i;

    for (i = 0; i < frames; i++)
    {
        /* 1 kHz square wave, ~1/16 full scale. */
        rt_int16_t s = (((phase0 + i) / 24) & 1) ? 2000 : -2000;

        p[i * 2] = s;
        p[i * 2 + 1] = s;
    }
}

static void tc_audio_playback(void)
{
    rt_device_t dev;
    struct rt_audio_caps caps;
    rt_uint32_t before, after;
    rt_uint32_t rate = 0, ch = 0;
    rt_tick_t deadline;

    dev = rt_device_find("sound0");
    uassert_not_null(dev);
    if (dev == RT_NULL)
    {
        return;
    }

    uassert_int_equal(rt_device_open(dev, RT_DEVICE_OFLAG_WRONLY), RT_EOK);

    rt_memset(&caps, 0, sizeof(caps));
    caps.main_type = AUDIO_TYPE_QUERY;
    uassert_int_equal(rt_device_control(dev, AUDIO_CTL_GETCAPS, &caps),
                      RT_EOK);
    uassert_true((caps.udata.mask & AUDIO_TYPE_OUTPUT) != 0);
    uassert_true((caps.udata.mask & AUDIO_TYPE_MIXER) != 0);

    /* Pick a format; unsupported rates must be rejected. */
    caps.main_type = AUDIO_TYPE_OUTPUT;
    caps.sub_type = AUDIO_DSP_PARAM;
    caps.udata.config.samplerate = 96000;
    caps.udata.config.channels = TC_AUDIO_CHANNELS;
    caps.udata.config.samplebits = 16;
    uassert_int_equal(rt_device_control(dev, AUDIO_CTL_CONFIGURE, &caps),
                      -RT_EINVAL);

    caps.udata.config.samplerate = TC_AUDIO_RATE;
    uassert_int_equal(rt_device_control(dev, AUDIO_CTL_CONFIGURE, &caps),
                      RT_EOK);

    aic_audio_get_config(&rate, &ch);
    uassert_int_equal((int)rate, TC_AUDIO_RATE);
    uassert_int_equal((int)ch, TC_AUDIO_CHANNELS);

    caps.main_type = AUDIO_TYPE_OUTPUT;
    caps.sub_type = AUDIO_DSP_PARAM;
    uassert_int_equal(rt_device_control(dev, AUDIO_CTL_GETCAPS, &caps),
                      RT_EOK);
    uassert_int_equal((int)caps.udata.config.samplerate, TC_AUDIO_RATE);
    uassert_int_equal((int)caps.udata.config.samplebits, 16);

    caps.main_type = AUDIO_TYPE_MIXER;
    caps.sub_type = AUDIO_MIXER_VOLUME;
    caps.udata.value = 50;
    uassert_int_equal(rt_device_control(dev, AUDIO_CTL_CONFIGURE, &caps),
                      RT_EOK);

    tc_audio_fill(tc_audio_buf, sizeof(tc_audio_buf), 0);

    before = aic_audio_period_count();
    uassert_int_equal((int)rt_device_write(dev, 0, tc_audio_buf,
                                           sizeof(tc_audio_buf)),
                      (int)sizeof(tc_audio_buf));

    deadline = rt_tick_get() + rt_tick_from_millisecond(1000);
    after = before;
    while (after == before && rt_tick_get() < deadline)
    {
        rt_thread_mdelay(10);
        after = aic_audio_period_count();
    }
    uassert_true(after > before);

    uassert_int_equal(rt_device_close(dev), RT_EOK);
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_audio_playback);
}
UTEST_TC_EXPORT(testcase, "bsp.d213.audio", RT_NULL, RT_NULL, 10);

#endif /* BSP_USING_AUDIO */
