/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * DMIC capture (dmic0) device test.
 *
 * Proves the software path and the hardware capture behaviour: the
 * device registers as a standard read-only audio device, negotiates
 * the input format, the DMIC decimator and RX FIFO raise DRQ, the
 * cyclic DMAC completes periods and the record pipe delivers samples.
 * It cannot prove acoustic input: that needs a known sound source at
 * the microphone.  "dmic_record" prints live level statistics for
 * manual checks.
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "utest.h"
#include "board.h"
#include "drv_dmic.h"

#ifdef BSP_USING_DMIC

#define TC_DMIC_RATE     16000
#define TC_DMIC_CHANNELS 1
#define TC_DMIC_BYTES    2048

static rt_uint8_t tc_dmic_buf[TC_DMIC_BYTES] __attribute__((aligned(64)));

static void tc_dmic_capture(void)
{
    rt_device_t dev;
    struct rt_audio_caps caps;
    rt_uint32_t before, after;
    rt_size_t got = 0;
    rt_tick_t deadline;
    rt_ssize_t n;
    rt_size_t i;
    rt_int16_t *pcm = (rt_int16_t *)tc_dmic_buf;
    rt_int32_t min = 32767, max = -32768;

    dev = rt_device_find("dmic0");
    uassert_not_null(dev);
    if (dev == RT_NULL)
    {
        return;
    }

    uassert_int_equal(rt_device_open(dev, RT_DEVICE_OFLAG_RDONLY), RT_EOK);

    rt_memset(&caps, 0, sizeof(caps));
    caps.main_type = AUDIO_TYPE_QUERY;
    uassert_int_equal(rt_device_control(dev, AUDIO_CTL_GETCAPS, &caps),
                      RT_EOK);
    uassert_true((caps.udata.mask & AUDIO_TYPE_INPUT) != 0);
    uassert_true((caps.udata.mask & AUDIO_TYPE_MIXER) != 0);

    /* Reject formats the codec cannot produce. */
    caps.main_type = AUDIO_TYPE_INPUT;
    caps.sub_type = AUDIO_DSP_PARAM;
    caps.udata.config.samplerate = 96000;
    caps.udata.config.channels = TC_DMIC_CHANNELS;
    caps.udata.config.samplebits = 16;
    uassert_int_equal(rt_device_control(dev, AUDIO_CTL_CONFIGURE, &caps),
                      -RT_EINVAL);

    caps.udata.config.samplerate = TC_DMIC_RATE;
    uassert_int_equal(rt_device_control(dev, AUDIO_CTL_CONFIGURE, &caps),
                      RT_EOK);

    /* Stereo and mono both work on the DMIC pair; end on mono. */
    caps.udata.config.channels = 2;
    uassert_int_equal(rt_device_control(dev, AUDIO_CTL_CONFIGURE, &caps),
                      RT_EOK);
    caps.udata.config.channels = TC_DMIC_CHANNELS;
    uassert_int_equal(rt_device_control(dev, AUDIO_CTL_CONFIGURE, &caps),
                      RT_EOK);

    caps.main_type = AUDIO_TYPE_INPUT;
    caps.sub_type = AUDIO_DSP_PARAM;
    uassert_int_equal(rt_device_control(dev, AUDIO_CTL_GETCAPS, &caps),
                      RT_EOK);
    uassert_int_equal((int)caps.udata.config.samplerate, TC_DMIC_RATE);
    uassert_int_equal((int)caps.udata.config.channels, TC_DMIC_CHANNELS);
    uassert_int_equal((int)caps.udata.config.samplebits, 16);

    caps.main_type = AUDIO_TYPE_MIXER;
    caps.sub_type = AUDIO_MIXER_VOLUME;
    caps.udata.value = 60;
    uassert_int_equal(rt_device_control(dev, AUDIO_CTL_CONFIGURE, &caps),
                      RT_EOK);
    caps.udata.value = 0;
    uassert_int_equal(rt_device_control(dev, AUDIO_CTL_GETCAPS, &caps),
                      RT_EOK);
    uassert_int_equal((int)caps.udata.value, 60);

    /*
     * Wait for the DMA to complete periods before reading: the record
     * pipe blocks a reader when empty, so prove data flows first.
     */
    before = aic_dmic_period_count();
    deadline = rt_tick_get() + rt_tick_from_millisecond(1000);
    while (aic_dmic_period_count() == before && rt_tick_get() < deadline)
    {
        rt_thread_mdelay(5);
    }
    after = aic_dmic_period_count();
    uassert_true(after > before);

    if (after > before)
    {
        deadline = rt_tick_get() + rt_tick_from_millisecond(1000);
        while (got < sizeof(tc_dmic_buf) && rt_tick_get() < deadline)
        {
            n = rt_device_read(dev, 0, tc_dmic_buf + got,
                               sizeof(tc_dmic_buf) - got);
            if (n <= 0)
            {
                break;
            }
            got += n;
        }
        uassert_int_equal((int)got, (int)sizeof(tc_dmic_buf));

        for (i = 0; i < got / 2; i++)
        {
            if (pcm[i] < min)
                min = pcm[i];
            if (pcm[i] > max)
                max = pcm[i];
        }
        rt_kprintf("tc_dmic: %u bytes, min %d max %d, periods +%u\n",
                   (unsigned int)got, min, max, after - before);
    }

    uassert_int_equal(rt_device_close(dev), RT_EOK);
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_dmic_capture);
}
UTEST_TC_EXPORT(testcase, "bsp.d213.dmic", RT_NULL, RT_NULL, 20);

#endif /* BSP_USING_DMIC */
