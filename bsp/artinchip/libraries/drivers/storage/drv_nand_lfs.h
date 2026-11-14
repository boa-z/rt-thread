/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Diagnostic interface for the LittleFS/bad-block layer on nand_scratch.
 */

#ifndef __DRV_NAND_LFS_H__
#define __DRV_NAND_LFS_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

struct aic_nand_lfs_info
{
    rt_uint16_t total;          /* physical scratch blocks */
    rt_uint16_t logical;        /* LittleFS blocks */
    rt_uint16_t spare;          /* spare blocks */
    rt_uint16_t bad;            /* blocks marked bad */
    rt_uint16_t slot[2];        /* physical BBT blocks */
    rt_uint32_t sequence;       /* BBT sequence number */
    rt_bool_t mounted;
};

int aic_nand_lfs_get_info(struct aic_nand_lfs_info *info);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_NAND_LFS_H__ */
