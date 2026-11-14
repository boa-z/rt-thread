/*
 * Copyright (c) 2020-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: <qi.xu@artinchip.com>
 */

#ifndef _ARTINCHIP_HAL_VE_H_
#define _ARTINCHIP_HAL_VE_H_

#define IOC_VE_WAIT         0x01
#define IOC_VE_GET_CLIENT   0x02
#define IOC_VE_PUT_CLIENT   0x03
#define IOC_VE_GET_INFO     0x04
#define IOC_VE_RESET        0x05

struct wait_info {
        int wait_time;
        unsigned int reg_status;
};

/* D213 transport ABI only. Register access and engine lifecycle are
 * provided by aic_drv_ve.h; codec implementations are external clients. */
struct aic_ve_client;

#endif
