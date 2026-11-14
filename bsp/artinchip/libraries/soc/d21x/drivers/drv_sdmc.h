/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D21x SDMC host driver helpers.
 */

#ifndef __DRV_SDMC_H__
#define __DRV_SDMC_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

struct rt_mmcsd_host;

/*
 * Return the MMCSD host of a probed SDMC instance: 1 = SDMC1 (SD card),
 * 2 = SDMC2 (SDIO slot).  Returns RT_NULL when that instance is not
 * enabled in Kconfig or its probe failed.
 *
 * SDIO cards have no block device, so a function driver or test needs
 * this to reach the card the core attached to the host.
 */
struct rt_mmcsd_host *aic_sdmc_host(unsigned int index);

/*
 * Print the clock tree, transfer-engine selection and controller
 * registers of one probed instance (1 = SDMC1, 2 = SDMC2).  Used by the
 * "sdmc_dump" command and by tests that record host state on failure.
 */
void aic_sdmc_dump(unsigned int index);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_SDMC_H__ */
