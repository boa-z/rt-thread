/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * Compatibility header.  The original vendor header is
 * bsp/artinchip/include/hal/aic_hal_reset.h.
 */

#ifndef __AIC_HAL_RESET_H__
#define __AIC_HAL_RESET_H__

#include <stdint.h>
#include "aic_compat.h"
#include "aic_reset_id.h"

#ifdef __cplusplus
extern "C" {
#endif

int hal_reset_assert(uint32_t rst_id);
int hal_reset_deassert(uint32_t rst_id);
int hal_reset_status(uint32_t rst_id);

#ifdef __cplusplus
}
#endif

#endif /* __AIC_HAL_RESET_H__ */
