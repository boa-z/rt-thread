/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * Compatibility header for the D21x syscfg block.  The original vendor
 * header is bsp/artinchip/include/hal/hal_syscfg.h.  Only the USB PHY
 * role switch and the USB reference-resistor calibration are needed by
 * this BSP, so the GMAC/efuse helpers are not declared here.
 */

#ifndef __ARTINCHIP_HAL_SYSCFG_H__
#define __ARTINCHIP_HAL_SYSCFG_H__

#include "aic_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

/* USB reference resistor calibration (vendor default 0x60). */
void hal_syscfg_usb0_res_cal(u32 res_val);
void hal_syscfg_usb1_res_cal(u32 res_val);

/* One-time PHY setup before the host controller is started. */
void hal_syscfg_usb_phy0_init(void);
void hal_syscfg_usb_phy1_init(void);

/* PHY0 role switch: nonzero selects host mode (DRD bit cleared). */
void hal_syscfg_usb_phy0_sw_host(s32 host_mode);

/* GMAC0 RMII/external-clock selection (D213ECV-DEMO-V4-0). */
void hal_syscfg_gmac0_init(void);

/*
 * The CherryUSB AIC host glue uses the shorter vendor name.
 */
static inline void syscfg_usb_phy0_sw_host(s32 host_mode)
{
    hal_syscfg_usb_phy0_sw_host(host_mode);
}

#ifdef __cplusplus
}
#endif

#endif /* __ARTINCHIP_HAL_SYSCFG_H__ */
