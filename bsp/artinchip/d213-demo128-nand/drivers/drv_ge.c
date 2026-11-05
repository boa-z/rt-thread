/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D21x GE Normal-mode adapter.
 *
 * The vendor GE HAL (vendor/ge/hal_ge_normal.c + hal_ge_hw.c) implements
 * fill, bitblt, scaling, Porter/Duff blending and rotation.  This file
 * adds the RT-Thread side: a "ge" device, serialization, physical-address
 * validation and cache maintenance.  The GE CMD queue is not compiled or
 * enabled.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>

#include "board.h"
#include "aic_cache.h"
#include "drv_ge.h"
#include "aic_hal_ge.h"

struct aic_ge
{
    struct rt_device dev;
    struct aic_ge_client *client;
    struct rt_mutex *lock;
    rt_uint32_t version;
    rt_bool_t inited;
};

static struct aic_ge g_ge;

static int ge_format_bpp(enum mpp_pixel_format format)
{
    switch (format)
    {
    case MPP_FMT_ARGB_8888:
    case MPP_FMT_ABGR_8888:
    case MPP_FMT_RGBA_8888:
    case MPP_FMT_BGRA_8888:
    case MPP_FMT_XRGB_8888:
    case MPP_FMT_XBGR_8888:
    case MPP_FMT_RGBX_8888:
    case MPP_FMT_BGRX_8888:
        return 4;
    case MPP_FMT_RGB_888:
    case MPP_FMT_BGR_888:
        return 3;
    case MPP_FMT_ARGB_1555:
    case MPP_FMT_ABGR_1555:
    case MPP_FMT_RGBA_5551:
    case MPP_FMT_BGRA_5551:
    case MPP_FMT_RGB_565:
    case MPP_FMT_BGR_565:
    case MPP_FMT_ARGB_4444:
    case MPP_FMT_ABGR_4444:
    case MPP_FMT_RGBA_4444:
    case MPP_FMT_BGRA_4444:
        return 2;
    default:
        return -1;
    }
}

static int ge_check_rgb_buf(const struct mpp_buf *buf)
{
    int bpp;
    rt_uint32_t bytes;

    if (buf == RT_NULL)
        return -RT_EINVAL;
    if (buf->buf_type != MPP_PHY_ADDR)
        return -RT_EINVAL;
    if (buf->phy_addr[0] == 0)
        return -RT_EINVAL;
    if (buf->size.width <= 0 || buf->size.height <= 0)
        return -RT_EINVAL;

    bpp = ge_format_bpp(buf->format);
    if (bpp < 0)
        return -RT_EINVAL;

    if (buf->stride[0] < (rt_uint32_t)buf->size.width * bpp)
        return -RT_EINVAL;

    bytes = buf->stride[0] * buf->size.height;
    if (bytes == 0)
        return -RT_EINVAL;

    return (int)bytes;
}

static int ge_rect_size(const struct mpp_buf *buf, int *w, int *h)
{
    if (buf->crop_en)
    {
        if (buf->crop.width <= 0 || buf->crop.height <= 0)
            return -RT_EINVAL;
        *w = buf->crop.width;
        *h = buf->crop.height;
    }
    else
    {
        *w = buf->size.width;
        *h = buf->size.height;
    }

    return 0;
}

/*
 * The vendor GE HAL returns -1 from ge_config_scaler() for RGB scaling
 * below 4x4, but ge_bitblt() does not check that return value and starts
 * the engine anyway, which leaves GE busy until timeout.  Reject the
 * descriptor here before it reaches the hardware.
 */
static int ge_check_scaler(const struct ge_bitblt *blt)
{
    int sw, sh, dw, dh;

    if (ge_rect_size(&blt->src_buf, &sw, &sh) != 0 ||
        ge_rect_size(&blt->dst_buf, &dw, &dh) != 0)
        return -RT_EINVAL;

    if ((sw != dw || sh != dh) &&
        (sw < 4 || sh < 4 || dw < 4 || dh < 4))
        return -RT_EINVAL;

    return 0;
}

static int ge_sync_before(const struct mpp_buf *buf)
{
    int bytes = ge_check_rgb_buf(buf);

    if (bytes < 0)
        return bytes;

    aic_dcache_clean_invalidate_range((uintptr_t)buf->phy_addr[0],
                                      (size_t)bytes);
    return 0;
}

static void ge_sync_after(const struct mpp_buf *buf)
{
    int bytes = ge_check_rgb_buf(buf);

    if (bytes > 0)
        aic_dcache_invalidate_range((uintptr_t)buf->phy_addr[0],
                                     (size_t)bytes);
}

int aic_ge_fillrect(struct ge_fillrect *fill)
{
    int ret;

    if (!g_ge.inited || fill == RT_NULL)
        return -RT_ERROR;

    ret = ge_sync_before(&fill->dst_buf);
    if (ret)
        return ret;

    rt_mutex_take(g_ge.lock, RT_WAITING_FOREVER);
    ret = hal_ge_control(g_ge.client, IOC_GE_FILLRECT, fill);
    ge_sync_after(&fill->dst_buf);
    rt_mutex_release(g_ge.lock);

    return ret == 0 ? RT_EOK : -RT_EIO;
}

int aic_ge_bitblt(struct ge_bitblt *blt)
{
    int ret;
    int ret2;

    if (!g_ge.inited || blt == RT_NULL)
        return -RT_ERROR;

    ret = ge_check_scaler(blt);
    if (ret)
        return ret;

    ret = ge_sync_before(&blt->src_buf);
    ret2 = ge_sync_before(&blt->dst_buf);
    if (ret || ret2)
        return ret ? ret : ret2;

    rt_mutex_take(g_ge.lock, RT_WAITING_FOREVER);
    ret = hal_ge_control(g_ge.client, IOC_GE_BITBLT, blt);
    ge_sync_after(&blt->dst_buf);
    rt_mutex_release(g_ge.lock);

    return ret == 0 ? RT_EOK : -RT_EIO;
}

int aic_ge_rotate(struct ge_rotation *rot)
{
    int ret;
    int ret2;

    if (!g_ge.inited || rot == RT_NULL)
        return -RT_ERROR;

    ret = ge_sync_before(&rot->src_buf);
    ret2 = ge_sync_before(&rot->dst_buf);
    if (ret || ret2)
        return ret ? ret : ret2;

    rt_mutex_take(g_ge.lock, RT_WAITING_FOREVER);
    ret = hal_ge_control(g_ge.client, IOC_GE_ROTATE, rot);
    ge_sync_after(&rot->dst_buf);
    rt_mutex_release(g_ge.lock);

    return ret == 0 ? RT_EOK : -RT_EIO;
}

int aic_ge_get_version(rt_uint32_t *version)
{
    if (!g_ge.inited || version == RT_NULL)
        return -RT_ERROR;

    *version = g_ge.version;
    return RT_EOK;
}

static rt_err_t ge_control(rt_device_t dev, int cmd, void *args)
{
    (void)dev;

    switch (cmd)
    {
    case IOC_GE_VERSION:
        if (args == RT_NULL)
            return -RT_EINVAL;
        *(rt_uint32_t *)args = g_ge.version;
        return RT_EOK;

    case IOC_GE_MODE:
        if (args == RT_NULL)
            return -RT_EINVAL;
        *(rt_uint32_t *)args = GE_MODE_NORMAL;
        return RT_EOK;

    case IOC_GE_FILLRECT:
        return aic_ge_fillrect((struct ge_fillrect *)args);

    case IOC_GE_BITBLT:
        return aic_ge_bitblt((struct ge_bitblt *)args);

    case IOC_GE_ROTATE:
        return aic_ge_rotate((struct ge_rotation *)args);

    case IOC_GE_SYNC:
        /* Normal mode operations are synchronous. */
        return RT_EOK;

    default:
        return -RT_EINVAL;
    }
}

static rt_err_t ge_open(rt_device_t dev, rt_uint16_t oflag)
{
    (void)dev;
    (void)oflag;
    return RT_EOK;
}

static rt_err_t ge_close(rt_device_t dev)
{
    (void)dev;
    return RT_EOK;
}

static const struct rt_device_ops ge_dev_ops =
{
    .init = RT_NULL,
    .open = ge_open,
    .close = ge_close,
    .read = RT_NULL,
    .write = RT_NULL,
    .control = ge_control,
};

int aic_ge_init(void)
{
    if (g_ge.inited)
        return RT_EOK;

    if (hal_ge_init() != 0)
    {
        rt_kprintf("ge: HAL init failed\n");
        return -RT_ERROR;
    }

    g_ge.client = hal_ge_open();
    g_ge.lock = rt_mutex_create("ge", RT_IPC_FLAG_PRIO);
    if (g_ge.client == RT_NULL || g_ge.lock == RT_NULL)
    {
        rt_kprintf("ge: client/ lock allocation failed\n");
        return -RT_ENOMEM;
    }

    if (hal_ge_control(g_ge.client, IOC_GE_VERSION, &g_ge.version) != 0)
    {
        rt_kprintf("ge: version read failed\n");
        return -RT_EIO;
    }

    g_ge.dev.type = RT_Device_Class_Miscellaneous;
    g_ge.dev.ops = &ge_dev_ops;
    g_ge.dev.user_data = &g_ge;

    if (rt_device_register(&g_ge.dev, "ge", RT_DEVICE_FLAG_RDWR) != RT_EOK)
    {
        rt_kprintf("ge: device register failed\n");
        return -RT_ERROR;
    }

    g_ge.inited = RT_TRUE;
    rt_kprintf("ge: Normal mode ready, version 0x%08x\n", g_ge.version);

    return RT_EOK;
}

int rt_hw_ge_init(void)
{
    return aic_ge_init();
}
INIT_DEVICE_EXPORT(rt_hw_ge_init);
