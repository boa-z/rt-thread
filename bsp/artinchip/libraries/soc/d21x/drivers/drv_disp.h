/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * D21x LVDS display / framebuffer public interface.
 */

#ifndef __DRV_DISP_H__
#define __DRV_DISP_H__

#include <rtthread.h>
#include <rtdevice.h>
#include "artinchip_fb.h"

#ifdef __cplusplus
extern "C" {
#endif

struct aic_disp_info
{
    rt_uint32_t width;
    rt_uint32_t height;
    rt_uint32_t bpp;
    rt_uint32_t stride;
    rt_uint32_t fb_count;
    rt_uint32_t fb_size;
    void *fb[2];
};

/* UI initially belongs to aic_disp_device() (the framebuffer/LVGL path).
 * A coordinator must stop that path before explicitly releasing its UI.
 * Tokens identify resources, not threads; one pipeline can span workers.
 * On update failure retain old/new buffers until a successful disable or
 * release. A failed release retains ownership and must be retried. */
int aic_disp_claim_layer(unsigned int layer_id, const void *owner);
int aic_disp_update_layer(const void *owner, const struct aicfb_layer_data *layer);
int aic_disp_release_layer(unsigned int layer_id, const void *owner);
int aic_disp_update_bg_blend(const void *owner, unsigned int enable);
int aic_disp_update_alpha(const void *owner, const struct aicfb_alpha_config *alpha);

int aic_disp_init(void);
struct rt_device *aic_disp_device(void);
int aic_disp_get_info(struct aic_disp_info *info);
void *aic_disp_get_framebuffer(int index);
int aic_disp_get_fb_count(void);
int aic_disp_pan(int index, int wait_vsync);
int aic_disp_wait_vsync(int timeout_ms);
int aic_disp_colorbar(int enable);
void aic_disp_cache_clean(int index);
void aic_disp_get_stats(rt_uint32_t *vsync_count,
                        rt_uint32_t *underflow_count, int *active);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_DISP_H__ */
