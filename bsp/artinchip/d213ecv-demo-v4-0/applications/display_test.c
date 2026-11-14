/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * MSH validation commands for the D21x DE/LVDS framebuffer and GE.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <stdlib.h>
#include <string.h>

#ifdef BSP_USING_DISPLAY
#include "drv_disp.h"
#include "aic_compat.h"
#include "aic_hal_clk.h"
#endif
#ifdef BSP_USING_GE
#include "drv_ge.h"
#endif

#ifdef BSP_USING_DISPLAY
static void disp_dump_info(void)
{
    struct aic_disp_info info;

    if (aic_disp_get_info(&info) != RT_EOK)
    {
        rt_kprintf("display not initialized\n");
        return;
    }

    rt_kprintf("display: %ux%u, %u bpp, stride %u\n",
               info.width, info.height, info.bpp, info.stride);
    rt_kprintf("buffers: %u x %u bytes\n", info.fb_count, info.fb_size);
    rt_kprintf("fb0=%p fb1=%p\n", info.fb[0], info.fb[1]);
    rt_kprintf("device lcd=%s\n",
               rt_device_find("lcd") ? "registered" : "missing");
}

static void cmd_disp_info(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    disp_dump_info();
}
MSH_CMD_EXPORT_ALIAS(cmd_disp_info, disp_info, show D21x display information);

static void cmd_disp_clk(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    rt_kprintf("PLL_FRA2 gen=0x%08x fra=0x%08x sdm=0x%08x\n",
               readl(CMU_BASE + PLL_FRA2_GEN_REG),
               readl(CMU_BASE + PLL_FRA2_CFG_REG),
               readl(CMU_BASE + PLL_FRA2_SDM_REG));
    rt_kprintf("CMU disp=0x%08x de=0x%08x ge=0x%08x lvds=0x%08x\n",
               readl(CMU_BASE + CLK_DISP_REG),
               readl(CMU_BASE + CLK_DE_REG),
               readl(CMU_BASE + CLK_GE_REG),
               readl(CMU_BASE + CLK_LVDS_REG));
}
MSH_CMD_EXPORT_ALIAS(cmd_disp_clk, disp_clk, dump D21x display and GE clock registers);

static void cmd_disp_colorbar(int argc, char **argv)
{
    int enable = (argc > 1) ? atoi(argv[1]) : 1;

    if (aic_disp_colorbar(enable) != RT_EOK)
    {
        rt_kprintf("color bar control failed\n");
        return;
    }
    rt_kprintf("hardware color bar %s\n", enable ? "on" : "off");
}
MSH_CMD_EXPORT_ALIAS(cmd_disp_colorbar, disp_colorbar,
                     toggle the DE hardware color bar);

static void fb_fill(void *fb, int width, int height, int stride,
                    rt_uint32_t color)
{
    int x;
    int y;
    rt_uint32_t *line;

    for (y = 0; y < height; y++)
    {
        line = (rt_uint32_t *)((char *)fb + y * stride);
        for (x = 0; x < width; x++)
            line[x] = color;
    }
}

static void cmd_disp_test(int argc, char **argv)
{
    struct aic_disp_info info;
    int ret;
    int i;

    (void)argc;
    (void)argv;

    disp_dump_info();
    if (aic_disp_get_info(&info) != RT_EOK)
        return;

    /* 1. Hardware-generated color bar. */
    if (aic_disp_colorbar(1) != RT_EOK)
        rt_kprintf("color bar: FAILED\n");
    else
    {
        rt_kprintf("color bar: on for 2 s\n");
        rt_thread_mdelay(2000);
        aic_disp_colorbar(0);
    }

    /* 2. Software fill and single/double buffer pan. */
    fb_fill(info.fb[0], info.width, info.height, info.stride, 0x00FF0000);
    ret = aic_disp_pan(0, 1);
    rt_kprintf("pan fb0 (red): %s\n", ret == RT_EOK ? "OK" : "timeout/error");
    rt_thread_mdelay(300);

    if (info.fb_count > 1)
    {
        fb_fill(info.fb[1], info.width, info.height, info.stride, 0x0000FF00);
        ret = aic_disp_pan(1, 1);
        rt_kprintf("pan fb1 (green): %s\n",
                   ret == RT_EOK ? "OK" : "timeout/error");
        rt_thread_mdelay(300);
    }

    /* 3. VSync measurement. */
    i = 0;
    while (i < 10)
    {
        if (aic_disp_wait_vsync(100) != RT_EOK)
            break;
        i++;
    }
    rt_kprintf("vsync: %d/10 received\n", i);

    {
        rt_uint32_t vsyncs;
        rt_uint32_t underflows;
        int active;

        aic_disp_get_stats(&vsyncs, &underflows, &active);
        rt_kprintf("display stats: vsync=%u underflow=%u active=%d\n",
                   vsyncs, underflows, active);
    }
}
MSH_CMD_EXPORT_ALIAS(cmd_disp_test, disp_test,
                     run LVDS color bar fill pan and VSync tests);
#endif

#ifdef BSP_USING_GE
#define GE_TEST_W 160
#define GE_TEST_H 120

static int ge_check_pixel(void *buf, int stride, int x, int y,
                          rt_uint32_t expect)
{
    rt_uint32_t value;

    value = *(rt_uint32_t *)((char *)buf + y * stride + x * 4);
    return (value == expect) ? 0 : -1;
}

static void ge_set_pattern(void *buf, int w, int h, int stride)
{
    int x;
    int y;

    for (y = 0; y < h; y++)
    {
        rt_uint32_t *line = (rt_uint32_t *)((char *)buf + y * stride);
        for (x = 0; x < w; x++)
            line[x] = (x & 1) ? 0x00FFFFFF : 0x00000000;
    }
}

/* Four uniform quadrants: interpolation cannot change their interiors. */
static void ge_set_quadrants(void *buf, int w, int h, int stride)
{
    int x;
    int y;

    for (y = 0; y < h; y++)
    {
        rt_uint32_t *line = (rt_uint32_t *)((char *)buf + y * stride);
        for (x = 0; x < w; x++)
        {
            if (x < w / 2)
                line[x] = (y < h / 2) ? 0x000000FF : 0x00FF0000;
            else
                line[x] = (y < h / 2) ? 0x0000FF00 : 0x00FFFFFF;
        }
    }
}

static void ge_set_halves(void *buf, int w, int h, int stride)
{
    int x;
    int y;

    for (y = 0; y < h; y++)
    {
        rt_uint32_t *line = (rt_uint32_t *)((char *)buf + y * stride);
        for (x = 0; x < w; x++)
            line[x] = (x < w / 2) ? 0x00FF0000 : 0x000000FF;
    }
}

static void cmd_ge_test(int argc, char **argv)
{
    struct ge_fillrect fill;
    struct ge_bitblt blt;
    void *src;
    void *dst;
    rt_uint32_t version = 0;
    int ret;

    (void)argc;
    (void)argv;

    src = rt_malloc_align(GE_TEST_W * GE_TEST_H * 4, 64);
    dst = rt_malloc_align(GE_TEST_W * 2 * GE_TEST_H * 2 * 4, 64);
    if (src == RT_NULL || dst == RT_NULL)
    {
        rt_kprintf("ge: test buffer allocation failed\n");
        if (src != RT_NULL)
            rt_free_align(src);
        if (dst != RT_NULL)
            rt_free_align(dst);
        return;
    }

    if (aic_ge_get_version(&version) != RT_EOK)
    {
        rt_kprintf("ge: version read failed\n");
        rt_free_align(src);
        rt_free_align(dst);
        return;
    }
    rt_kprintf("ge: version 0x%08x\n", version);

    /* FillRect. */
    memset(&fill, 0, sizeof(fill));
    fill.type = GE_NO_GRADIENT;
    fill.start_color = 0x000000FF; /* BGR memory order: blue */
    fill.dst_buf.buf_type = MPP_PHY_ADDR;
    fill.dst_buf.phy_addr[0] = (uintptr_t)dst;
    fill.dst_buf.stride[0] = GE_TEST_W * 4;
    fill.dst_buf.size.width = GE_TEST_W;
    fill.dst_buf.size.height = GE_TEST_H;
    fill.dst_buf.format = MPP_FMT_ARGB_8888;

    ret = aic_ge_fillrect(&fill);
    if (ret == RT_EOK &&
        ge_check_pixel(dst, GE_TEST_W * 4, 5, 5, 0x000000FF) == 0)
        rt_kprintf("ge: fillrect PASS\n");
    else
        rt_kprintf("ge: fillrect FAIL (ret %d)\n", ret);

    /* BitBlt without scaling. */
    ge_set_pattern(src, GE_TEST_W, GE_TEST_H, GE_TEST_W * 4);
    memset(&blt, 0, sizeof(blt));
    blt.src_buf.buf_type = MPP_PHY_ADDR;
    blt.src_buf.phy_addr[0] = (uintptr_t)src;
    blt.src_buf.stride[0] = GE_TEST_W * 4;
    blt.src_buf.size.width = GE_TEST_W;
    blt.src_buf.size.height = GE_TEST_H;
    blt.src_buf.format = MPP_FMT_ARGB_8888;
    blt.dst_buf = blt.src_buf;
    blt.dst_buf.phy_addr[0] = (uintptr_t)dst;

    ret = aic_ge_bitblt(&blt);
    if (ret == RT_EOK &&
        ge_check_pixel(dst, GE_TEST_W * 4, 0, 0, 0x00000000) == 0 &&
        ge_check_pixel(dst, GE_TEST_W * 4, 1, 0, 0x00FFFFFF) == 0)
        rt_kprintf("ge: bitblt PASS\n");
    else
        rt_kprintf("ge: bitblt FAIL (ret %d)\n", ret);

    /* Scaling: uniform quadrants make the result independent of the
       interpolation kernel; check the interior of each quadrant. */
    ge_set_quadrants(src, GE_TEST_W, GE_TEST_H, GE_TEST_W * 4);
    blt.dst_buf.size.width = GE_TEST_W * 2;
    blt.dst_buf.size.height = GE_TEST_H * 2;
    blt.dst_buf.stride[0] = GE_TEST_W * 2 * 4;
    ret = aic_ge_bitblt(&blt);
    if (ret == RT_EOK &&
        ge_check_pixel(dst, GE_TEST_W * 2 * 4, 40, 30, 0x000000FF) == 0 &&
        ge_check_pixel(dst, GE_TEST_W * 2 * 4, 280, 30, 0x0000FF00) == 0 &&
        ge_check_pixel(dst, GE_TEST_W * 2 * 4, 40, 210, 0x00FF0000) == 0 &&
        ge_check_pixel(dst, GE_TEST_W * 2 * 4, 280, 210, 0x00FFFFFF) == 0)
        rt_kprintf("ge: scale 2x PASS\n");
    else
        rt_kprintf("ge: scale 2x FAIL (ret %d)\n", ret);

    /* Alpha blending. */
    memset(&fill, 0, sizeof(fill));
    fill.type = GE_NO_GRADIENT;
    fill.start_color = 0x000000FF;
    fill.dst_buf = blt.src_buf;
    fill.dst_buf.phy_addr[0] = (uintptr_t)dst;
    aic_ge_fillrect(&fill);

    memset(src, 0, GE_TEST_W * GE_TEST_H * 4);
    {
        int x;
        int y;

        for (y = 0; y < 16; y++)
        {
            rt_uint32_t *line = (rt_uint32_t *)((char *)src + y * GE_TEST_W * 4);
            for (x = 0; x < 16; x++)
                line[x] = 0x80FF0000; /* 50% alpha red */
        }
    }
    blt.src_buf.phy_addr[0] = (uintptr_t)src;
    blt.src_buf.crop_en = 1;
    blt.src_buf.crop.x = 0;
    blt.src_buf.crop.y = 0;
    blt.src_buf.crop.width = 16;
    blt.src_buf.crop.height = 16;
    blt.dst_buf = blt.src_buf;
    blt.dst_buf.phy_addr[0] = (uintptr_t)dst;
    blt.ctrl.alpha_en = 1;
    blt.ctrl.alpha_rules = GE_PD_SRC_OVER;
    ret = aic_ge_bitblt(&blt);
    if (ret == RT_EOK)
    {
        rt_uint32_t value = *(rt_uint32_t *)dst;

        if ((value & 0x00FF0000) && (value & 0x000000FF))
            rt_kprintf("ge: blend PASS (0x%08x)\n", value);
        else
            rt_kprintf("ge: blend FAIL (0x%08x)\n", value);
    }
    else
    {
        rt_kprintf("ge: blend FAILED (ret %d)\n", ret);
    }

    /* Rotation by 90 degrees.  The left half of the source is red and
       the right half is blue, so after rotation the two halves must sit
       on opposite sides of the long axis.  The GE direction (clockwise
       or counter-clockwise) is reported rather than assumed. */
    ge_set_halves(src, GE_TEST_W, GE_TEST_H, GE_TEST_W * 4);
    memset(&blt, 0, sizeof(blt));
    blt.src_buf.buf_type = MPP_PHY_ADDR;
    blt.src_buf.phy_addr[0] = (uintptr_t)src;
    blt.src_buf.stride[0] = GE_TEST_W * 4;
    blt.src_buf.size.width = GE_TEST_W;
    blt.src_buf.size.height = GE_TEST_H;
    blt.src_buf.format = MPP_FMT_ARGB_8888;
    blt.dst_buf = blt.src_buf;
    blt.dst_buf.phy_addr[0] = (uintptr_t)dst;
    blt.dst_buf.size.width = GE_TEST_H;
    blt.dst_buf.size.height = GE_TEST_W;
    blt.dst_buf.stride[0] = GE_TEST_H * 4;
    blt.ctrl.flags = MPP_ROTATION_90;
    ret = aic_ge_bitblt(&blt);
    if (ret == RT_EOK)
    {
        rt_uint32_t top = *(rt_uint32_t *)((char *)dst +
                            40 * GE_TEST_H * 4 + 60 * 4);
        rt_uint32_t bottom = *(rt_uint32_t *)((char *)dst +
                            120 * GE_TEST_H * 4 + 60 * 4);

        if (top == 0x00FF0000 && bottom == 0x000000FF)
            rt_kprintf("ge: rotate 90 PASS (clockwise)\n");
        else if (top == 0x000000FF && bottom == 0x00FF0000)
            rt_kprintf("ge: rotate 90 PASS (counter-clockwise)\n");
        else
            rt_kprintf("ge: rotate 90 FAIL (top 0x%08x bottom 0x%08x)\n",
                       top, bottom);
    }
    else
    {
        rt_kprintf("ge: rotate 90 FAILED (ret %d)\n", ret);
    }

    rt_free_align(src);
    rt_free_align(dst);
}
MSH_CMD_EXPORT_ALIAS(cmd_ge_test, ge_test,
                     run GE fillrect bitblt scale blend and rotate tests);
#endif
