/*
 * Copyright (c) 2026 RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 * On-board resource/parameter tests. No codec or camera is required.
 */
#include <rtthread.h>
#include "utest.h"
#include "aic_buffer.h"

static void tc_buffer_validation(void)
{
    struct mpp_buf buf = {0};
    struct aic_buffer_layout layout;
    buf.buf_type = MPP_PHY_ADDR;
    buf.format = MPP_FMT_NV12;
    buf.size.width = 64;
    buf.size.height = 32;
    buf.stride[0] = buf.stride[1] = 64;
    buf.phy_addr[0] = 0x40000000;
    buf.phy_addr[1] = 0x40001000;
    uassert_int_equal(aic_buffer_layout(&buf, &layout), 0);
    uassert_int_equal(layout.length[1], 1024);
    buf.phy_addr[1] = 0xfffffff0;
    uassert_true(aic_buffer_layout(&buf, &layout) != 0);
}
#ifdef BSP_USING_VE
#include "aic_drv_ve.h"
static void tc_ve_resource(void)
{
    struct aic_ve_client *client = drv_ve_open();
    uassert_not_null(client);
    if (!client) return;
    uassert_int_equal(drv_ve_control(client, IOC_VE_GET_CLIENT, RT_NULL), 0);
    uassert_int_equal(drv_ve_control(client, IOC_VE_GET_CLIENT, RT_NULL), -RT_EBUSY);
    uassert_int_equal(drv_ve_close(client), -RT_EBUSY);
    uassert_int_equal(drv_ve_control(client, IOC_VE_RESET, RT_NULL), 0);
    uassert_int_equal(drv_ve_control(client, IOC_VE_PUT_CLIENT, RT_NULL), 0);
    uassert_int_equal(drv_ve_close(client), 0);
}
#endif

#ifdef BSP_USING_DISPLAY
#include "drv_disp.h"
static void tc_display_owner(void)
{
    static int owner, other;
    int ret;
    uassert_int_equal(aic_disp_claim_layer(AICFB_LAYER_TYPE_UI, &owner), -RT_EBUSY);
    ret = aic_disp_claim_layer(AICFB_LAYER_TYPE_VIDEO, &owner);
    uassert_int_equal(ret, 0);
    if (ret) return;
    uassert_int_equal(aic_disp_claim_layer(AICFB_LAYER_TYPE_VIDEO, &other), -RT_EBUSY);
    uassert_int_equal(aic_disp_release_layer(AICFB_LAYER_TYPE_VIDEO, &other), -RT_EBUSY);
    uassert_int_equal(aic_disp_release_layer(AICFB_LAYER_TYPE_VIDEO, &owner), 0);
}
#endif

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_buffer_validation);
#ifdef BSP_USING_VE
    UTEST_UNIT_RUN(tc_ve_resource);
#endif
#ifdef BSP_USING_DISPLAY
    UTEST_UNIT_RUN(tc_display_owner);
#endif
}
UTEST_TC_EXPORT(testcase, "bsp.d213.multimedia", RT_NULL, RT_NULL, 10);
