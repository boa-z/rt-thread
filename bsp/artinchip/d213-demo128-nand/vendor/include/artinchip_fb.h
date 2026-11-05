/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal framebuffer API compatibility header.  The GE HAL does not use
 * the vendor framebuffer structures; the BSP display driver uses
 * mpp_types.h directly.
 */

#ifndef _UAPI__ARTINCHIP_FB_H_
#define _UAPI__ARTINCHIP_FB_H_

#include "mpp_types.h"

#define AICFB_PIXEL_ALPHA_MODE      0
#define AICFB_GLOBAL_ALPHA_MODE     1
#define AICFB_MIXDER_ALPHA_MODE     2

enum gamma_lut
{
    GAMMA_RED,
    GAMMA_GREEN,
    GAMMA_BLUE,
};

#endif /* _UAPI__ARTINCHIP_FB_H_ */
