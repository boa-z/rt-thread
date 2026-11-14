/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D21x DMIC capture driver.
 *
 * The register programming lives in libraries/soc/d21x/hal/hal_audio.c (curated from
 * Luban-Lite, Apache-2.0).  This file adapts the digital-microphone
 * receive path to the standard RT-Thread audio device "dmic0"
 * (read-only) and manages:
 *
 *   - the cyclic DMA ring that drains the DMIC RX FIFO,
 *   - the IRQ-side period hand-off into the RT-Thread record pipe.
 *
 * The audio core owns the record pipe: the driver calls
 * rt_audio_rx_done() for every finished DMA period and the framework
 * copies that period into the pipe drained by rt_device_read().  The
 * C906 D-Cache is not coherent, so the ring is invalidated before the
 * CPU reads a finished period.
 *
 * Playback ("sound0") and capture share one codec block, its module
 * clock and PLL_FRA1, so the two streams are mutually exclusive: starting
 * capture stops a running replay stream, and configuring capture while
 * playback runs returns -RT_EBUSY (the audio core ignores start() errors,
 * but configure() results reach the caller).
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <stdlib.h>

#include "aic_core.h"
#include <interrupt.h>
#include "aic_soc.h"
#include "hal_audio.h"
#include "drv_audio.h"
#include "drv_dma.h"
#include "drv_dmic.h"

#ifndef RT_AUDIO_RECORD_PIPE_SIZE
#define RT_AUDIO_RECORD_PIPE_SIZE 2048
#endif

/*
 * Hand one pipe-sized period to the record pipe at a time.  The size is
 * rounded down to a whole cache line so the DMA ring keeps the C906
 * cache-maintenance contract even if the pipe size is configured oddly.
 */
#define DMIC_PERIOD_LEN  (RT_AUDIO_RECORD_PIPE_SIZE & ~(AIC_DMA_CACHE_LINE - 1))
#define DMIC_PERIOD_COUNT 2
#define DMIC_BUF_LEN     (DMIC_PERIOD_LEN * DMIC_PERIOD_COUNT)

#if DMIC_PERIOD_LEN < AIC_DMA_CACHE_LINE
#error "RT_AUDIO_RECORD_PIPE_SIZE must be at least one cache line"
#endif

#define DMIC_DEFAULT_RATE       16000
#define DMIC_DEFAULT_CHANNELS   1
#define DMIC_DEFAULT_VOLUME     100         /* 0..100, 100 = 0 dB */

struct aic_dmic_dev
{
    struct rt_audio_device audio;
    aic_audio_ctrl codec;
    rt_uint32_t volume;
    volatile rt_uint32_t periods;
    rt_uint32_t period_index;
    rt_bool_t running;
};

static struct aic_dmic_dev aic_dmic;
static rt_uint8_t dmic_rx_fifo[DMIC_BUF_LEN]
    __attribute__((aligned(AIC_DMA_CACHE_LINE)));

/* Rates the codec module clock can produce exactly. */
static rt_bool_t aic_dmic_rate_supported(rt_uint32_t rate)
{
    switch (rate)
    {
    case 8000:
    case 11025:
    case 12000:
    case 16000:
    case 22050:
    case 24000:
    case 32000:
    case 44100:
    case 48000:
        return RT_TRUE;
    default:
        return RT_FALSE;
    }
}

static void aic_dmic_set_volume(struct aic_dmic_dev *dmic, rt_uint32_t volume)
{
    rt_uint32_t reg_volume;

    if (volume > AUDIO_VOLUME_MAX)
        volume = AUDIO_VOLUME_MAX;
    dmic->volume = volume;

    /* 0 = mute, AUDIO_VOLUME_MAX = MAX_VOLUME_0DB (0 dB). */
    reg_volume = (volume * MAX_VOLUME_0DB) / AUDIO_VOLUME_MAX;
    hal_audio_set_dmic_volume(&dmic->codec, reg_volume);
}

static void aic_dmic_period_cb(aic_audio_ctrl *codec, void *arg)
{
    struct aic_dmic_dev *dmic;
    rt_uint8_t *period;

    if ((unsigned long)arg != AUDIO_RX_DMIC_PERIOD_INT)
        return;

    dmic = rt_container_of(codec, struct aic_dmic_dev, codec);

    /*
     * The DMAC wrote the ring while the CPU was not looking; drop the
     * stale cache lines of both periods before the pipe copies one.
     */
    aic_dma_cache_invalidate(dmic_rx_fifo, DMIC_BUF_LEN);

    period = &dmic_rx_fifo[dmic->period_index * DMIC_PERIOD_LEN];
    dmic->period_index = (dmic->period_index + 1) % DMIC_PERIOD_COUNT;
    dmic->periods++;

    /* Framework copies the period into the record pipe and wakes the
     * reader; safe from interrupt context (the pipe forces writes). */
    rt_audio_rx_done(&dmic->audio, period, DMIC_PERIOD_LEN);
}

static rt_err_t aic_dmic_hw_start(struct aic_dmic_dev *dmic)
{
    aic_audio_ctrl *codec = &dmic->codec;

    if (dmic->running)
        return RT_EOK;

    /*
     * Reapply the format on every start: playback shares the codec
     * registers, so the previous stream's programming cannot be assumed.
     */
    hal_audio_set_samplerate(codec, codec->config.samplerate);
    hal_audio_set_dmic_channel(codec, codec->config.channel);
    aic_dmic_set_volume(dmic, dmic->volume);

    dmic->period_index = 0;
    dmic->periods = 0;

    /*
     * Playback may have registered its own callback on the shared codec
     * since the device was opened, so claim it back on every start.
     */
    hal_audio_attach_callback(codec, aic_dmic_period_cb, RT_NULL);

    hal_audio_dmic_start(codec);
    if (codec->dmic_info.dma_chan == RT_NULL)
    {
        pr_err("dmic: no DMA channel for capture\n");
        return -RT_EIO;
    }

    dmic->running = RT_TRUE;
    return RT_EOK;
}

static void aic_dmic_hw_stop(struct aic_dmic_dev *dmic)
{
    if (!dmic->running)
        return;

    hal_audio_dmic_stop(&dmic->codec);
    dmic->running = RT_FALSE;
}

void aic_dmic_stop_stream(void)
{
    aic_dmic_hw_stop(&aic_dmic);
}

static rt_err_t aic_dmic_set_format(struct aic_dmic_dev *dmic,
                                    rt_uint32_t rate, rt_uint32_t channels)
{
    aic_audio_ctrl *codec = &dmic->codec;
    rt_bool_t restart = RT_FALSE;

    if (!aic_dmic_rate_supported(rate))
        return -RT_EINVAL;
    if (channels != 1 && channels != 2)
        return -RT_EINVAL;

#ifdef BSP_USING_AUDIO
    if (aic_audio_is_playing())
        return -RT_EBUSY;
#endif

    if (rate == codec->config.samplerate && channels == codec->config.channel)
        return RT_EOK;

    if (dmic->running)
    {
        aic_dmic_hw_stop(dmic);
        restart = RT_TRUE;
    }

    hal_audio_set_samplerate(codec, rate);
    hal_audio_set_dmic_channel(codec, channels);
    codec->config.samplerate = rate;
    codec->config.channel = channels;
    codec->config.samplebits = 16;

    if (restart)
        return aic_dmic_hw_start(dmic);

    return RT_EOK;
}

static rt_err_t aic_dmic_init(struct rt_audio_device *audio)
{
    struct aic_dmic_dev *dmic = rt_container_of(audio, struct aic_dmic_dev,
                                                audio);
    aic_audio_ctrl *codec = &dmic->codec;

    codec->dmic_info.buf_info.buf = dmic_rx_fifo;
    codec->dmic_info.buf_info.buf_len = DMIC_BUF_LEN;
    codec->dmic_info.buf_info.period_len = DMIC_PERIOD_LEN;

    codec->config.samplerate = DMIC_DEFAULT_RATE;
    codec->config.channel = DMIC_DEFAULT_CHANNELS;
    codec->config.samplebits = 16;

    return RT_EOK;
}

static rt_err_t aic_dmic_getcaps(struct rt_audio_device *audio,
                                 struct rt_audio_caps *caps)
{
    struct aic_dmic_dev *dmic = rt_container_of(audio, struct aic_dmic_dev,
                                                audio);
    aic_audio_ctrl *codec = &dmic->codec;

    switch (caps->main_type)
    {
    case AUDIO_TYPE_QUERY:
        caps->udata.mask = AUDIO_TYPE_INPUT | AUDIO_TYPE_MIXER;
        return RT_EOK;

    case AUDIO_TYPE_INPUT:
        switch (caps->sub_type)
        {
        case AUDIO_DSP_PARAM:
            caps->udata.config.samplerate = codec->config.samplerate;
            caps->udata.config.channels = codec->config.channel;
            caps->udata.config.samplebits = 16;
            return RT_EOK;
        case AUDIO_DSP_SAMPLERATE:
            caps->udata.config.samplerate = codec->config.samplerate;
            return RT_EOK;
        case AUDIO_DSP_CHANNELS:
            caps->udata.config.channels = codec->config.channel;
            return RT_EOK;
        case AUDIO_DSP_SAMPLEBITS:
            caps->udata.config.samplebits = 16;
            return RT_EOK;
        default:
            break;
        }
        break;

    case AUDIO_TYPE_MIXER:
        switch (caps->sub_type)
        {
        case AUDIO_MIXER_QUERY:
            caps->udata.mask = AUDIO_MIXER_VOLUME;
            return RT_EOK;
        case AUDIO_MIXER_VOLUME:
            caps->udata.value = dmic->volume;
            return RT_EOK;
        default:
            break;
        }
        break;

    default:
        break;
    }

    return -RT_ERROR;
}

static rt_err_t aic_dmic_configure(struct rt_audio_device *audio,
                                   struct rt_audio_caps *caps)
{
    struct aic_dmic_dev *dmic = rt_container_of(audio, struct aic_dmic_dev,
                                                audio);
    aic_audio_ctrl *codec = &dmic->codec;

    switch (caps->main_type)
    {
    case AUDIO_TYPE_INPUT:
        switch (caps->sub_type)
        {
        case AUDIO_DSP_PARAM:
            if (caps->udata.config.samplebits != 16)
                return -RT_EINVAL;
            return aic_dmic_set_format(dmic,
                                       caps->udata.config.samplerate,
                                       caps->udata.config.channels);

        case AUDIO_DSP_SAMPLERATE:
            return aic_dmic_set_format(dmic,
                                       caps->udata.config.samplerate,
                                       codec->config.channel);

        case AUDIO_DSP_CHANNELS:
            return aic_dmic_set_format(dmic,
                                       codec->config.samplerate,
                                       caps->udata.config.channels);

        case AUDIO_DSP_SAMPLEBITS:
            /* The DMIC decimator path is fixed at 16 bits. */
            return (caps->udata.config.samplebits == 16) ? RT_EOK
                                                         : -RT_EINVAL;
        default:
            return -RT_ERROR;
        }

    case AUDIO_TYPE_MIXER:
        if (caps->sub_type == AUDIO_MIXER_VOLUME)
        {
            aic_dmic_set_volume(dmic, caps->udata.value);
            return RT_EOK;
        }
        return -RT_ERROR;

    default:
        return -RT_ERROR;
    }
}

static rt_err_t aic_dmic_start(struct rt_audio_device *audio, int stream)
{
    struct aic_dmic_dev *dmic = rt_container_of(audio, struct aic_dmic_dev,
                                                audio);

    if (stream != AUDIO_STREAM_RECORD)
        return -RT_EINVAL;

#ifdef BSP_USING_AUDIO
    /*
     * Both paths program the same codec module clock and FS registers, so
     * capture cannot start beside a running replay stream without running
     * it at the wrong rate.  The audio core ignores start() errors, so
     * refusing here would silently open a dead capture device; stop
     * playback and take the codec over instead.
     */
    if (aic_audio_is_playing())
    {
        pr_warn("dmic: stopping playback to take over the codec\n");
        aic_audio_stop_stream();
    }
#endif

    return aic_dmic_hw_start(dmic);
}

static rt_err_t aic_dmic_stop(struct rt_audio_device *audio, int stream)
{
    struct aic_dmic_dev *dmic = rt_container_of(audio, struct aic_dmic_dev,
                                                audio);

    if (stream != AUDIO_STREAM_RECORD)
        return -RT_EINVAL;

    aic_dmic_hw_stop(dmic);
    return RT_EOK;
}

static const struct rt_audio_ops aic_dmic_ops =
{
    .getcaps = aic_dmic_getcaps,
    .configure = aic_dmic_configure,
    .init = aic_dmic_init,
    .start = aic_dmic_start,
    .stop = aic_dmic_stop,
    .transmit = RT_NULL,
    /* A record-only device has no replay ring for the core to query. */
    .buffer_info = RT_NULL,
};

rt_uint32_t aic_dmic_period_count(void)
{
    return aic_dmic.periods;
}

rt_bool_t aic_dmic_is_capturing(void)
{
    return aic_dmic.running;
}

static int rt_hw_dmic_init(void)
{
    aic_audio_ctrl *codec = &aic_dmic.codec;
    rt_err_t ret;

    aic_dmic.volume = DMIC_DEFAULT_VOLUME;

    if (hal_audio_init(codec) != 0)
    {
        pr_err("dmic: codec clock/reset init failed\n");
        return -RT_EIO;
    }

    aic_dmic.audio.ops = (struct rt_audio_ops *)&aic_dmic_ops;

    ret = rt_audio_register(&aic_dmic.audio, "dmic0", RT_DEVICE_FLAG_RDONLY,
                            &aic_dmic);
    if (ret != RT_EOK)
    {
        pr_err("dmic: device registration failed (%d)\n", ret);
        return ret;
    }

    pr_info("dmic: dmic0 ready (%u Hz, %u ch)\n",
            codec->config.samplerate, codec->config.channel);

    return 0;
}
INIT_DEVICE_EXPORT(rt_hw_dmic_init);

/* ------------------------------------------------------------------ */
/* Manual level check                                                  */
/* ------------------------------------------------------------------ */

static rt_uint32_t aic_dmic_isqrt(rt_uint32_t v)
{
    rt_uint32_t r = 0;
    rt_uint32_t bit = 1u << 30;

    while (bit > v)
        bit >>= 2;
    while (bit)
    {
        if (v >= r + bit)
        {
            v -= r + bit;
            r = (r >> 1) + bit;
        }
        else
        {
            r >>= 1;
        }
        bit >>= 2;
    }

    return r;
}

static int cmd_dmic_record(int argc, char **argv)
{
    rt_device_t dev;
    struct rt_audio_caps caps;
    static rt_int16_t buf[DMIC_PERIOD_LEN / 2];
    rt_uint32_t rate = DMIC_DEFAULT_RATE;
    rt_uint32_t ch = DMIC_DEFAULT_CHANNELS;
    rt_uint32_t ms = 1000;
    rt_uint32_t total, got = 0, samples = 0;
    rt_uint32_t before, after;
    rt_int32_t min[2] = {32767, 32767}, max[2] = {-32768, -32768};
    rt_int64_t sum[2] = {0, 0}, sumsq[2] = {0, 0};
    rt_uint32_t cnt[2] = {0, 0};
    rt_size_t i;

    if (argc > 1)
        ms = (rt_uint32_t)atoi(argv[1]);
    if (argc > 2)
        ch = (rt_uint32_t)atoi(argv[2]);
    if (ms == 0 || ms > 10000 || ch < 1 || ch > 2)
    {
        rt_kprintf("usage: dmic_record [ms 1..10000] [channels 1..2]\n");
        return -1;
    }

    dev = rt_device_find("dmic0");
    if (dev == RT_NULL)
    {
        rt_kprintf("dmic0 not found (BSP_USING_DMIC off?)\n");
        return -1;
    }
    if (rt_device_open(dev, RT_DEVICE_OFLAG_RDONLY) != RT_EOK)
    {
        rt_kprintf("dmic0 open failed\n");
        return -1;
    }

    rt_memset(&caps, 0, sizeof(caps));
    caps.main_type = AUDIO_TYPE_INPUT;
    caps.sub_type = AUDIO_DSP_PARAM;
    caps.udata.config.samplerate = rate;
    caps.udata.config.channels = ch;
    caps.udata.config.samplebits = 16;
    rt_device_control(dev, AUDIO_CTL_CONFIGURE, &caps);

    total = (rate * ch * 2 * ms) / 1000;
    before = aic_dmic_period_count();
    while (got < total)
    {
        rt_size_t want = total - got;
        rt_ssize_t n;

        if (want > sizeof(buf))
            want = sizeof(buf);
        n = rt_device_read(dev, 0, buf, want);
        if (n <= 0)
            continue;

        for (i = 0; i < (rt_size_t)n / 2; i++)
        {
            rt_uint32_t c = i % ch;
            rt_int32_t s = buf[i];

            if (s < min[c])
                min[c] = s;
            if (s > max[c])
                max[c] = s;
            sum[c] += s;
            sumsq[c] += (rt_int64_t)s * s;
            cnt[c]++;
        }
        samples += (rt_size_t)n / 2;
        got += n;
    }
    after = aic_dmic_period_count();

    rt_device_close(dev);

    if (samples == 0)
    {
        rt_kprintf("dmic_record: no data\n");
        return -1;
    }

    for (i = 0; i < ch; i++)
    {
        rt_kprintf("dmic_record: ch%u min %d max %d mean %d rms %u\n",
                   (unsigned)i, min[i], max[i],
                   (rt_int32_t)(sum[i] / cnt[i]),
                   aic_dmic_isqrt((rt_uint32_t)(sumsq[i] / cnt[i])));
    }
    rt_kprintf("dmic_record: %u ms, %u samples, dma periods +%u\n",
               ms, samples, after - before);

    return 0;
}
MSH_CMD_EXPORT_ALIAS(cmd_dmic_record, dmic_record,
                     capture from dmic0 and print level statistics);
