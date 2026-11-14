/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 * Board wiring mapped to the unchanged vendor GMAC HAL's configuration ABI.
 */
#ifndef __AIC_BOARD_CONFIG_H__
#define __AIC_BOARD_CONFIG_H__

#ifdef BSP_USING_GMAC0
#define AIC_DEV_GMAC0_RMII
#define AIC_DEV_GMAC0_PHY_EXTCLK
#define AIC_DEV_GMAC0_PHYADDR      BSP_GMAC0_PHYADDR
#define AIC_DEV_GMAC0_PHYRST_GPIO  BSP_GMAC0_PHYRST_GPIO
#define AIC_DEV_GMAC0_MACADDR      BSP_GMAC0_MACADDR
#endif

#endif /* __AIC_BOARD_CONFIG_H__ */
