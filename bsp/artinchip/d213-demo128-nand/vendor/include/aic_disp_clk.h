/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * D21x display clock setup for LVDS.
 *
 * This performs the small, well-defined part of the vendor CMU that the
 * display path needs: program PLL_FRA2 to the LVDS serial clock, then
 * derive SCLK and PIXCLK from it.  It is intentionally not a general
 * clock framework; other peripherals keep their bootloader-programmed
 * clocks.
 */

#ifndef __AIC_DISP_CLK_H__
#define __AIC_DISP_CLK_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * pixclk: pixel clock in Hz
 * link_mode: 0 = single link 0, 1 = single link 1, 3 = dual link
 * sclk_out: optional, receives the LVDS serial clock
 */
int aic_disp_clk_set(uint32_t pixclk, int link_mode, uint32_t *sclk_out);

#ifdef __cplusplus
}
#endif

#endif /* __AIC_DISP_CLK_H__ */
