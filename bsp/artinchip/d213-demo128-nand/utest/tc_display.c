/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Non-destructive display information and VSync timing check.  Visual
 * quality (color bar, panning, underflow) remains a manual test because
 * it needs a human or a camera at the panel.
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "utest.h"

#ifdef BSP_USING_DISPLAY
#include "drv_disp.h"

static void tc_display_vsync(void)
{
    struct aic_disp_info info;
    rt_uint32_t vsync_before = 0;
    rt_uint32_t vsync_after = 0;
    rt_uint32_t underflow = 0;
    int active = 0;
    int ret;

    uassert_not_null(rt_device_find("lcd"));

    ret = aic_disp_get_info(&info);
    uassert_int_equal(ret, RT_EOK);
    if (ret != RT_EOK)
    {
        return;
    }

    uassert_int_equal(info.width, 1024);
    uassert_int_equal(info.height, 600);
    uassert_int_equal(info.bpp, 32);
    uassert_true(info.fb_count >= 1);
    uassert_true(info.stride >= info.width * 4);
    uassert_not_null(info.fb[0]);

    aic_disp_get_stats(&vsync_before, &underflow, &active);
    uassert_true(active >= 0);
    uassert_true(active < (int)info.fb_count);

    ret = aic_disp_wait_vsync(100);
    uassert_int_equal(ret, RT_EOK);
    if (ret == RT_EOK)
    {
        aic_disp_get_stats(&vsync_after, &underflow, &active);
        uassert_true(vsync_after > vsync_before);
    }
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_display_vsync);
}
UTEST_TC_EXPORT(testcase, "bsp.d213.display.vsync", RT_NULL, RT_NULL, 10);

#endif /* BSP_USING_DISPLAY */
