/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __DRV_QSPI_H__
#define __DRV_QSPI_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Register the QSPI0 bus ("qspi0").  Idempotent; the SPI NAND driver calls
 * it as an explicit dependency before attaching its device.
 */
rt_err_t rt_hw_qspi0_init(void);

/* Read-only QSPI0 register dump for diagnostics. */
void rt_hw_qspi0_dump(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_QSPI_H__ */
