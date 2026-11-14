/*
 * Copyright (c) 2022, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: <qi.xu@artinchip.com>
 */

#ifndef AIC_DRV_VE_H
#define AIC_DRV_VE_H
#include <stdint.h>
#include "aic_hal_ve.h"

#ifdef __cplusplus
extern "C" {
#endif

struct aic_ve_info
{
    uintptr_t reg_base;
    unsigned long clock_hz;
    unsigned int timeout_ms;
};

/* A single opener owns the engine service; GET/WAIT/RESET/PUT must be
 * called by the same thread. PUT aborts unfinished transactions. After
 * starting additional DMA following a completed WAIT, a failed client must
 * explicitly RESET before PUT.
 * All waits are bounded to 2000 ms. Do not mix direct and rt_device opens. */

struct aic_ve_client *drv_ve_open(void);
int drv_ve_close(struct aic_ve_client *client);
int drv_ve_control(struct aic_ve_client *client, int cmd, void *arg);

#ifdef __cplusplus
}
#endif

#endif /* AIC_DRV_VE_H */
