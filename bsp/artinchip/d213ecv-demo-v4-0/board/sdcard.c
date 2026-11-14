/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 * SD-card filesystem mount policy for the D213ECV-DEMO-V4-0 board.
 */
#include <rtthread.h>
#include <rtdevice.h>
#include "aic_core.h"

#if defined(BSP_SDCARD_AUTOMOUNT) && defined(BSP_USING_SDMC1)
#include <dfs_fs.h>

#ifndef BSP_SDCARD_MOUNT_POINT
#define BSP_SDCARD_MOUNT_POINT "/"
#endif

static rt_err_t aic_sdcard_mount(void)
{
    if (dfs_mount("sd0", BSP_SDCARD_MOUNT_POINT, "elm", 0, 0) == 0)
    {
        pr_info("sdmc1: FAT filesystem mounted at %s\n",
                BSP_SDCARD_MOUNT_POINT);
        return RT_EOK;
    }

    pr_warn("sdmc1: no mountable FAT filesystem at %s "
            "(card is not formatted or not ready)\n",
            BSP_SDCARD_MOUNT_POINT);
    return -RT_ERROR;
}

static void aic_sdcard_mount_thread(void *parameter)
{
    int tries = 50; /* up to 10 s for the MMCSD detect thread */

    (void)parameter;

    while (tries--)
    {
        if (rt_device_find("sd0"))
        {
            /* The block device appears only after probe completed. */
            aic_sdcard_mount();
            return;
        }
        rt_thread_mdelay(200);
    }
}

static int aic_sdcard_mount_start(void)
{
    rt_thread_t tid;

    tid = rt_thread_create("sd_mount", aic_sdcard_mount_thread, RT_NULL,
                           4096, 20, 10);
    if (tid)
    {
        rt_thread_startup(tid);
    }
    else
    {
        pr_err("sdmc1: cannot create mount thread\n");
    }

    return 0;
}
INIT_APP_EXPORT(aic_sdcard_mount_start);
#endif /* BSP_SDCARD_AUTOMOUNT */


#ifdef RT_USING_FINSH
#if defined(BSP_SDCARD_AUTOMOUNT) && defined(BSP_USING_SDMC1)
static void sd_mount(void)
{
    if (!rt_device_find("sd0"))
    {
        rt_kprintf("sd0 not present; run sd_rescan first\n");
        return;
    }

    aic_sdcard_mount();
}
MSH_CMD_EXPORT(sd_mount, mount the SD card FAT filesystem);
#endif
#endif
