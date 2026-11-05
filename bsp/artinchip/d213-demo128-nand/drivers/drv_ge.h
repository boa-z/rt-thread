/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * D21x GE Normal-mode public interface.
 */

#ifndef __DRV_GE_H__
#define __DRV_GE_H__

#include <rtthread.h>
#include "aic_compat.h"
#include "aic_drv_ge.h"

#ifdef __cplusplus
extern "C" {
#endif

int aic_ge_init(void);
int aic_ge_get_version(rt_uint32_t *version);
int aic_ge_fillrect(struct ge_fillrect *fill);
int aic_ge_bitblt(struct ge_bitblt *blt);
int aic_ge_rotate(struct ge_rotation *rot);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_GE_H__ */
