/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D21x audio codec playback driver (DSPK / PWM speaker path).
 *
 * The register programming lives in libraries/soc/d21x/hal/hal_audio.c
 * (curated from Luban-Lite, Apache-2.0).  This file adapts it to the
 * standard RT-Thread audio device ("sound0") and manages:
 *
 *   - PLL_FRA1 and the codec divider
 *     (libraries/soc/d21x/port/aic_hal_clk.c),
 *   - the speaker selection, amplifier polarity and settling delay supplied
 *     by the board descriptor,
 *   - the cyclic DMA ring that feeds the codec TX FIFO.
 *
 * The framework copies queued application data into the ring exported by
 * buffer_info(); transmit() only has to make each finished period
 * visible to the DMAC (the C906 D-Cache is not coherent).
 *
 * Only playback is implemented here; DMIC capture is a separate device
 * ("dmic0", drv_dmic.c).  Both streams share the codec block, its module
 * clock and its single IRQ callback, so they are mutually exclusive: the
 * stream that starts second takes the codec over.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <stdlib.h>

#include "aic_core.h"
#include "aic_board.h"
#include "hal_audio.h"
#include "drv_audio.h"
#include "drv_dma.h"
#ifdef BSP_USING_DMIC
#include "drv_dmic.h"
#endif

#ifndef RT_AUDIO_REPLAY_MP_BLOCK_SIZE
#define RT_AUDIO_REPLAY_MP_BLOCK_SIZE 4096
#endif

#define AUDIO_TX_PERIODS        2
#define AUDIO_TX_PERIOD_SIZE    RT_AUDIO_REPLAY_MP_BLOCK_SIZE
#define AUDIO_TX_BUF_SIZE       (AUDIO_TX_PERIOD_SIZE * AUDIO_TX_PERIODS)

#define AUDIO_DEFAULT_RATE      48000
#define AUDIO_DEFAULT_CHANNELS  2
#define AUDIO_DEFAULT_VOLUME    80          /* 0..100 */

struct aic_audio_dev
{
    struct rt_audio_device audio;
    aic_audio_ctrl codec;
    rt_base_t pa_pin;
    rt_uint32_t volume;
    volatile rt_bool_t playing;
    volatile rt_uint32_t periods;
};

static struct aic_audio_dev aic_snd;
static rt_uint8_t audio_tx_fifo[AUDIO_TX_BUF_SIZE]
    __attribute__((aligned(AIC_DMA_CACHE_LINE)));

/* The board supplies amplifier enable polarity. */
static void aic_audio_pa(rt_bool_t on)
{
    if (aic_snd.pa_pin < 0)
        return;

    rt_pin_write(aic_snd.pa_pin, (!!on ^ !!aic_board_audio.pa_active_low)
                 ? PIN_HIGH : PIN_LOW);
}

static void aic_audio_set_volume(rt_uint32_t volume)
{
    rt_uint32_t reg_volume;

    if (volume > AUDIO_VOLUME_MAX)
        volume = AUDIO_VOLUME_MAX;
    aic_snd.volume = volume;

    /* 0 = mute, AUDIO_VOLUME_MAX = FADE_MAX_CONTROL (0 dB). */
    reg_volume = (volume * FADE_MAX_CONTROL) / AUDIO_VOLUME_MAX;
    hal_audio_set_fade_volume(&aic_snd.codec, reg_volume);
}

static void aic_audio_spk_on(void)
{
    if (aic_board_audio.speaker == 0)
    {
        hal_audio_set_playback_by_spk0(&aic_snd.codec);
        hal_audio_disable_fade_ch1(&aic_snd.codec);
    }
    else
    {
        hal_audio_set_playback_by_spk1(&aic_snd.codec);
        hal_audio_disable_fade_ch0(&aic_snd.codec);
    }
}

/* Rates the codec PLL/divider pair can produce exactly. */
static rt_bool_t aic_audio_rate_supported(rt_uint32_t rate)
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

static void aic_audio_period_cb(aic_audio_ctrl *codec, void *arg)
{
    struct aic_audio_dev *snd;
    unsigned long event = (unsigned long)arg;

    snd = rt_container_of(codec, struct aic_audio_dev, codec);

    if (event == AUDIO_TX_PERIOD_INT)
    {
        snd->periods++;
        rt_audio_tx_complete(&snd->audio);
    }
}

static rt_err_t aic_audio_init(struct rt_audio_device *audio)
{
    struct aic_audio_dev *snd = rt_container_of(audio, struct aic_audio_dev,
                                                audio);
    aic_audio_ctrl *codec = &snd->codec;

    codec->tx_info.buf_info.buf = audio_tx_fifo;
    codec->tx_info.buf_info.buf_len = AUDIO_TX_BUF_SIZE;
    codec->tx_info.buf_info.period_len = AUDIO_TX_PERIOD_SIZE;

    codec->config.samplerate = AUDIO_DEFAULT_RATE;
    codec->config.channel = AUDIO_DEFAULT_CHANNELS;
    codec->config.samplebits = 16;

    return RT_EOK;
}

static void aic_audio_buffer_info(struct rt_audio_device *audio,
                                  struct rt_audio_buf_info *info)
{
    (void)audio;

    info->buffer = audio_tx_fifo;
    info->block_size = AUDIO_TX_PERIOD_SIZE;
    info->block_count = AUDIO_TX_PERIODS;
    info->total_size = AUDIO_TX_BUF_SIZE;
}

static rt_err_t aic_audio_getcaps(struct rt_audio_device *audio,
                                  struct rt_audio_caps *caps)
{
    struct aic_audio_dev *snd = rt_container_of(audio, struct aic_audio_dev,
                                                audio);

    (void)audio;

    switch (caps->main_type)
    {
    case AUDIO_TYPE_QUERY:
        caps->udata.mask = AUDIO_TYPE_OUTPUT | AUDIO_TYPE_MIXER;
        return RT_EOK;

    case AUDIO_TYPE_OUTPUT:
        switch (caps->sub_type)
        {
        case AUDIO_DSP_PARAM:
            caps->udata.config.samplerate = snd->codec.config.samplerate;
            caps->udata.config.channels = snd->codec.config.channel;
            caps->udata.config.samplebits = 16;
            return RT_EOK;
        case AUDIO_DSP_SAMPLERATE:
            caps->udata.config.samplerate = snd->codec.config.samplerate;
            return RT_EOK;
        case AUDIO_DSP_CHANNELS:
            caps->udata.config.channels = snd->codec.config.channel;
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
            caps->udata.value = snd->volume;
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

static rt_err_t aic_audio_configure(struct rt_audio_device *audio,
                                    struct rt_audio_caps *caps)
{
    struct aic_audio_dev *snd = rt_container_of(audio, struct aic_audio_dev,
                                                audio);
    aic_audio_ctrl *codec = &snd->codec;

    switch (caps->main_type)
    {
    case AUDIO_TYPE_OUTPUT:
        switch (caps->sub_type)
        {
        case AUDIO_DSP_PARAM:
        case AUDIO_DSP_SAMPLERATE:
        {
            rt_uint32_t rate = caps->udata.config.samplerate;

            if (rate == 0)
                return -RT_EINVAL;
            if (!aic_audio_rate_supported(rate))
                return -RT_EINVAL;
            if (snd->playing && rate != codec->config.samplerate)
                return -RT_EBUSY;
#ifdef BSP_USING_DMIC
            /* The DMIC stream runs off the same codec sample rate. */
            if (aic_dmic_is_capturing() &&
                rate != codec->config.samplerate)
                return -RT_EBUSY;
#endif

            if (rate != codec->config.samplerate)
            {
                hal_audio_set_samplerate(codec, rate);
                codec->config.samplerate = rate;
            }
            if (caps->sub_type == AUDIO_DSP_SAMPLERATE)
                break;
        }
        /* fall through: AUDIO_DSP_PARAM also sets the channel count */
        /* FALLTHROUGH */
        case AUDIO_DSP_CHANNELS:
        {
            rt_uint32_t ch = caps->udata.config.channels;

            if (ch != 1 && ch != 2)
                return -RT_EINVAL;
            if (snd->playing && ch != codec->config.channel)
                return -RT_EBUSY;
#ifdef BSP_USING_DMIC
            if (aic_dmic_is_capturing() && ch != codec->config.channel)
                return -RT_EBUSY;
#endif

            if (ch != codec->config.channel)
            {
                hal_audio_set_playback_channel(codec, ch);
                codec->config.channel = ch;
            }
            break;
        }
        case AUDIO_DSP_SAMPLEBITS:
            /* The codec path is fixed at 16 bits. */
            if (caps->udata.config.samplebits != 16)
                return -RT_EINVAL;
            break;
        default:
            return -RT_ERROR;
        }
        break;

    case AUDIO_TYPE_MIXER:
        switch (caps->sub_type)
        {
        case AUDIO_MIXER_VOLUME:
            aic_audio_set_volume(caps->udata.value);
            break;
        default:
            return -RT_ERROR;
        }
        break;

    default:
        return -RT_ERROR;
    }

    return RT_EOK;
}

static rt_err_t aic_audio_start(struct rt_audio_device *audio, int stream)
{
    struct aic_audio_dev *snd = rt_container_of(audio, struct aic_audio_dev,
                                                audio);
    aic_audio_ctrl *codec = &snd->codec;

    if (stream != AUDIO_STREAM_REPLAY)
        return -RT_EINVAL;
    if (snd->playing)
        return RT_EOK;

#ifdef BSP_USING_DMIC
    /*
     * The codec clock tree is shared with capture.  The audio core ignores
     * start() errors, so refusing while dmic0 records would leave a replay
     * stream that never plays; stop the capture stream and take over.
     */
    if (aic_dmic_is_capturing())
    {
        pr_warn("audio: stopping dmic capture to take over the codec\n");
        aic_dmic_stop_stream();
    }
#endif

    /*
     * A capture session may have reprogrammed the shared codec FS and
     * module clock since the last replay, so apply our format again.
     */
    hal_audio_set_samplerate(codec, codec->config.samplerate);
    hal_audio_set_playback_channel(codec, codec->config.channel);
    hal_audio_enable_fade(codec);
    aic_audio_set_volume(snd->volume);
    aic_audio_spk_on();
    hal_audio_attach_callback(codec, aic_audio_period_cb, NULL);

    aic_audio_pa(RT_TRUE);
    rt_thread_mdelay(aic_board_audio.pa_settle_ms);

    /*
     * Fill the first period before the DMAC starts, otherwise the
     * speaker plays whatever the buffer held at boot.
     */
    snd->periods = 0;
    rt_audio_tx_complete(audio);
    hal_audio_playback_start(codec);

    if (codec->tx_info.dma_chan == RT_NULL)
    {
        pr_err("audio: no DMA channel for playback\n");
        aic_audio_pa(RT_FALSE);
        return -RT_EIO;
    }

    snd->playing = RT_TRUE;
    return RT_EOK;
}

void aic_audio_stop_stream(void)
{
    aic_audio_ctrl *codec = &aic_snd.codec;

    if (!aic_snd.playing)
        return;

    hal_audio_detach_callback(codec);
    if (codec->tx_info.dma_chan)
        hal_audio_playback_stop(codec);
    hal_audio_disable_fade(codec);
    aic_audio_pa(RT_FALSE);
    aic_snd.playing = RT_FALSE;
}

static rt_err_t aic_audio_stop(struct rt_audio_device *audio, int stream)
{
    if (stream != AUDIO_STREAM_REPLAY)
        return -RT_EINVAL;

    aic_audio_stop_stream();
    return RT_EOK;
}

static rt_ssize_t aic_audio_transmit(struct rt_audio_device *audio,
                                     const void *write_buf, void *read_buf,
                                     rt_size_t size)
{
    rt_ubase_t offset;

    (void)read_buf;

    if (write_buf == RT_NULL || size == 0)
        return 0;

    offset = (rt_ubase_t)write_buf - (rt_ubase_t)audio_tx_fifo;
    if (offset >= AUDIO_TX_BUF_SIZE)
    {
        pr_err("audio: transmit outside TX ring\n");
        return 0;
    }

    /* The DMAC reads the ring; flush the period the CPU just wrote. */
    aic_dma_cache_clean(write_buf, size);
    return size;
}

static const struct rt_audio_ops aic_audio_ops =
{
    .getcaps = aic_audio_getcaps,
    .configure = aic_audio_configure,
    .init = aic_audio_init,
    .start = aic_audio_start,
    .stop = aic_audio_stop,
    .transmit = aic_audio_transmit,
    .buffer_info = aic_audio_buffer_info,
};

rt_uint32_t aic_audio_period_count(void)
{
    return aic_snd.periods;
}

void aic_audio_get_config(rt_uint32_t *samplerate, rt_uint32_t *channels)
{
    if (samplerate)
        *samplerate = aic_snd.codec.config.samplerate;
    if (channels)
        *channels = aic_snd.codec.config.channel;
}

rt_bool_t aic_audio_is_playing(void)
{
    return aic_snd.playing;
}

static int rt_hw_audio_init(void)
{
    aic_audio_ctrl *codec = &aic_snd.codec;
    rt_err_t ret;

    if (!aic_board_audio.pa_pin || aic_board_audio.speaker > 1)
        return -RT_EINVAL;
    aic_snd.volume = AUDIO_DEFAULT_VOLUME;
    aic_snd.pa_pin = rt_pin_get(aic_board_audio.pa_pin);

    /*
     * Set the output latch before the pin becomes an output, so the
     * amplifier stays muted through the transition.
     */
    if (aic_snd.pa_pin >= 0)
    {
        aic_audio_pa(RT_FALSE);
        rt_pin_mode(aic_snd.pa_pin, PIN_MODE_OUTPUT);
    }

    if (hal_audio_init(codec) != 0)
    {
        pr_err("audio: codec clock/reset init failed\n");
        return -RT_EIO;
    }

    hal_audio_set_playback_channel(codec, codec->config.channel);
    hal_audio_set_samplerate(codec, codec->config.samplerate);
    hal_audio_set_fade_control(codec, FADE_CTRL0_DEFAULT_SPEED,
                               FADE_CTRL0_DEFAULT_STEP);
    aic_audio_set_volume(aic_snd.volume);

    aic_snd.audio.ops = (struct rt_audio_ops *)&aic_audio_ops;

    ret = rt_audio_register(&aic_snd.audio, "sound0", RT_DEVICE_FLAG_WRONLY,
                            &aic_snd);
    if (ret != RT_EOK)
    {
        pr_err("audio: device registration failed (%d)\n", ret);
        return ret;
    }

    pr_info("audio: sound0 ready (%u Hz, %u ch, SPK%u, PA %s)\n",
            codec->config.samplerate, codec->config.channel,
            aic_board_audio.speaker,
            aic_board_audio.pa_pin);

    return 0;
}
INIT_DEVICE_EXPORT(rt_hw_audio_init);

/* ------------------------------------------------------------------ */
/* Manual tone generator                                               */
/* ------------------------------------------------------------------ */

/* sin(2*pi*i/64) * 32767 for i = 0..16; mirrored by the caller. */
static const rt_int16_t aic_sine_tab[17] =
{
    0, 3212, 6393, 9512, 12539, 15446, 18204, 20787, 23170,
    25329, 27245, 28898, 30273, 31356, 32137, 32609, 32767,
};

static rt_int16_t aic_sine(rt_uint32_t phase)
{
    rt_uint32_t idx = phase >> 26;      /* 0..63 */
    rt_int16_t v;

    if (idx < 16)
        v = aic_sine_tab[idx];
    else if (idx < 32)
        v = aic_sine_tab[32 - idx];
    else if (idx < 48)
        v = (rt_int16_t)-aic_sine_tab[idx - 32];
    else
        v = (rt_int16_t)-aic_sine_tab[64 - idx];

    return v;
}

static int cmd_audio_tone(int argc, char **argv)
{
    rt_device_t dev;
    struct rt_audio_caps caps;
    static rt_int16_t buf[AUDIO_TX_PERIOD_SIZE / 2];
    rt_uint32_t freq = 1000;
    rt_uint32_t ms = 1000;
    rt_uint32_t total, written = 0, phase = 0, inc;
    rt_uint32_t i;

    if (argc > 1)
        freq = (rt_uint32_t)atoi(argv[1]);
    if (argc > 2)
        ms = (rt_uint32_t)atoi(argv[2]);
    if (freq < 20 || freq > 20000 || ms == 0 || ms > 60000)
    {
        rt_kprintf("usage: audio_tone [freq_hz 20..20000] [ms 1..60000]\n");
        return -1;
    }

    dev = rt_device_find("sound0");
    if (dev == RT_NULL)
    {
        rt_kprintf("sound0 not found (BSP_USING_AUDIO off?)\n");
        return -1;
    }
    if (rt_device_open(dev, RT_DEVICE_OFLAG_WRONLY) != RT_EOK)
    {
        rt_kprintf("sound0 open failed\n");
        return -1;
    }

    rt_memset(&caps, 0, sizeof(caps));
    caps.main_type = AUDIO_TYPE_OUTPUT;
    caps.sub_type = AUDIO_DSP_PARAM;
    caps.udata.config.samplerate = AUDIO_DEFAULT_RATE;
    caps.udata.config.channels = AUDIO_DEFAULT_CHANNELS;
    caps.udata.config.samplebits = 16;
    rt_device_control(dev, AUDIO_CTL_CONFIGURE, &caps);

    caps.main_type = AUDIO_TYPE_MIXER;
    caps.sub_type = AUDIO_MIXER_VOLUME;
    caps.udata.value = AUDIO_DEFAULT_VOLUME;
    rt_device_control(dev, AUDIO_CTL_CONFIGURE, &caps);

    /* phase increment for one sample, 32-bit accumulator */
    inc = (rt_uint32_t)(((rt_uint64_t)freq << 32) / AUDIO_DEFAULT_RATE);

    total = (AUDIO_DEFAULT_RATE * AUDIO_DEFAULT_CHANNELS * 2 * ms) / 1000;
    while (written < total)
    {
        rt_uint32_t chunk = total - written;

        if (chunk > sizeof(buf))
            chunk = sizeof(buf);

        for (i = 0; i < chunk / 4; i++)
        {
            rt_int16_t sample = (rt_int16_t)(aic_sine(phase) >> 2);

            buf[i * 2] = sample;
            buf[i * 2 + 1] = sample;
            phase += inc;
        }

        if (rt_device_write(dev, 0, buf, chunk) != chunk)
            break;
        written += chunk;
    }

    rt_device_close(dev);
    return 0;
}
MSH_CMD_EXPORT_ALIAS(cmd_audio_tone, audio_tone,
                     play a sine tone on sound0: audio_tone [freq] [ms]);
