/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Read-only helpers for the ArtInChip Bootloader A/B contract.
 *
 * These functions read the 'env'/'env_r' MTD partitions and the FIT
 * headers of 'os'/'os_r'.  They never program, erase or write an env
 * variable.  Writing os/os_r or env requires a separate, explicitly
 * authorised OTA implementation.
 */

#ifndef __DRV_OTA_SLOT_H__
#define __DRV_OTA_SLOT_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Number of redundant env copies (env, env_r). */
#define AIC_OTA_ENV_COPIES 2

struct aic_ota_env_status
{
    rt_bool_t   read_ok[AIC_OTA_ENV_COPIES];
    rt_bool_t   valid[AIC_OTA_ENV_COPIES];
    rt_uint8_t  flags[AIC_OTA_ENV_COPIES];
    rt_uint32_t crc[AIC_OTA_ENV_COPIES];
    rt_int8_t   active;      /* 0 = env, 1 = env_r, -1 = neither valid */
};

/*
 * Read and validate both env copies.  Returns RT_EOK when the metadata
 * was collected (check 'active'), -RT_ENODEV when an MTD partition is
 * missing and -RT_EIO when neither copy can be read.
 */
int aic_ota_env_status(struct aic_ota_env_status *status);

/*
 * Look up a variable in the active env copy.  The returned value is
 * copied with a trailing NUL into 'value'.  Returns RT_EOK, -RT_ENOENT
 * when the key is absent, or another negative error.
 */
int aic_ota_env_get(const char *name, char *value, rt_size_t size);

/* Print every key=value pair from the active env copy. */
int aic_ota_env_dump(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_OTA_SLOT_H__ */
