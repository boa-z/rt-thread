/*
 * Copyright (c) 2026 RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 * Linear GE/DE descriptor validation. No OS or multimedia package dependency.
 */
#ifndef AIC_BUFFER_H
#define AIC_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include "mpp_types.h"

/* Check RV64 addresses before narrowing. D213 currently has identity mapping;
 * this is not an MMU translator or a proof of the caller's allocation size. */
static inline int aic_dma_address(uintptr_t address, size_t size, uint32_t *physical)
{
    if (!physical || !address || !size || address > UINT32_MAX ||
        size - 1 > UINT32_MAX - address)
        return -EINVAL;
    *physical = (uint32_t)address;
    return 0;
}

struct aic_buffer_layout
{
    unsigned int planes;
    unsigned int bytes_per_pixel;
    unsigned int sub_x;
    unsigned int sub_y;
    uint32_t length[3];
};

static inline int aic_buffer_layout(const struct mpp_buf *b,
                                     struct aic_buffer_layout *out)
{
    struct aic_buffer_layout l = {0};
    unsigned int w, h, i;
    unsigned int row[3] = {0}, rows[3] = {0};

    if (!b || !out || b->buf_type != MPP_PHY_ADDR ||
        b->size.width <= 0 || b->size.height <= 0)
        return -EINVAL;
    w = b->size.width;
    h = b->size.height;
    if (w > 4096 || h > 4096)
        return -ERANGE;
    l.planes = 1;
    l.sub_x = l.sub_y = 1;
    if (b->format >= MPP_FMT_ARGB_8888 && b->format <= MPP_FMT_BGRX_8888)
        l.bytes_per_pixel = 4;
    else if (b->format == MPP_FMT_RGB_888 || b->format == MPP_FMT_BGR_888)
        l.bytes_per_pixel = 3;
    else if (b->format >= MPP_FMT_ARGB_1555 && b->format <= MPP_FMT_BGRA_4444)
        l.bytes_per_pixel = 2;
    else
    {
        switch (b->format)
        {
        case MPP_FMT_YUV420P:
            l.planes = 3;
            l.sub_x = l.sub_y = 2;
            break;
        case MPP_FMT_YUV422P:
            l.planes = 3;
            l.sub_x = 2;
            break;
        case MPP_FMT_YUV444P:
            l.planes = 3;
            break;
        case MPP_FMT_NV12:
        case MPP_FMT_NV21:
            l.planes = 2;
            l.sub_x = l.sub_y = 2;
            break;
        case MPP_FMT_NV16:
        case MPP_FMT_NV61:
            l.planes = 2;
            l.sub_x = 2;
            break;
        case MPP_FMT_YUV400:
            l.bytes_per_pixel = 1;
            break;
        case MPP_FMT_YUYV:
        case MPP_FMT_YVYU:
        case MPP_FMT_UYVY:
        case MPP_FMT_VYUY:
            l.bytes_per_pixel = 2;
            l.sub_x = 2;
            break;
        default:
            return -ENOTSUP;
        }
    }
    if (w % l.sub_x || h % l.sub_y)
        return -EINVAL;
    row[0] = w * (l.bytes_per_pixel ? l.bytes_per_pixel : 1);
    rows[0] = h;
    for (i = 1; i < l.planes; i++)
    {
        row[i] = l.planes == 2 ? w : w / l.sub_x;
        rows[i] = h / l.sub_y;
    }
    for (i = 0; i < l.planes; i++)
    {
        uint64_t len = (uint64_t)b->stride[i] * rows[i];
        if (b->stride[i] < row[i] || b->stride[i] > 65535 || !b->phy_addr[i] ||
            !len || len > INT32_MAX || len - 1 > UINT32_MAX - b->phy_addr[i])
            return -EINVAL;
        l.length[i] = (uint32_t)len;
    }
    /* Both engines have one shared chroma stride register for U and V. */
    if (l.planes == 3 && b->stride[1] != b->stride[2])
        return -EINVAL;
    if (b->crop_en)
    {
        if (b->crop.x < 0 || b->crop.y < 0 || b->crop.width <= 0 || b->crop.height <= 0 ||
            (unsigned int)b->crop.x >= w || (unsigned int)b->crop.y >= h ||
            (unsigned int)b->crop.width > w - b->crop.x ||
            (unsigned int)b->crop.height > h - b->crop.y ||
            b->crop.x % l.sub_x || b->crop.y % l.sub_y ||
            b->crop.width % l.sub_x || b->crop.height % l.sub_y)
            return -EINVAL;
    }
    *out = l;
    return 0;
}
#endif /* AIC_BUFFER_H */
