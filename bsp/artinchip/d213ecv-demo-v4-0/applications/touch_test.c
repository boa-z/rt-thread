/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * GT911 touch test command.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <stdlib.h>

static const char *touch_event_name(rt_uint8_t event)
{
    switch (event)
    {
    case RT_TOUCH_EVENT_DOWN:
        return "DOWN";
    case RT_TOUCH_EVENT_MOVE:
        return "MOVE";
    case RT_TOUCH_EVENT_UP:
        return "UP";
    default:
        return "NONE";
    }
}

static void cmd_touch_test(int argc, char **argv)
{
    struct rt_touch_data data[5];
    struct rt_touch_info info;
    rt_device_t dev;
    rt_tick_t stop;
    rt_uint32_t seconds = 10;

    dev = rt_device_find("gt911");
    if (dev == RT_NULL)
    {
        rt_kprintf("gt911 not found\n");
        return;
    }

    if (argc > 1)
        seconds = (rt_uint32_t)strtoul(argv[1], RT_NULL, 0);
    if (seconds == 0 || seconds > 60)
        seconds = 10;

    if (rt_device_open(dev, RT_DEVICE_FLAG_RDWR) != RT_EOK)
    {
        rt_kprintf("open gt911 failed\n");
        return;
    }

    rt_memset(&info, 0, sizeof(info));
    if (rt_device_control(dev, RT_TOUCH_CTRL_GET_INFO, &info) == RT_EOK)
    {
        rt_kprintf("gt911: range %dx%d, points %d\n",
                   info.range_x, info.range_y, info.point_num);
    }

    rt_kprintf("touch_test: touch the panel for %u s\n", seconds);
    stop = rt_tick_get() + seconds * RT_TICK_PER_SECOND;

    while (rt_tick_get() < stop)
    {
        rt_size_t num = rt_device_read(dev, 0, data,
                                       sizeof(data) / sizeof(data[0]));
        rt_size_t i;

        for (i = 0; i < num; i++)
        {
            if (data[i].event == RT_TOUCH_EVENT_NONE)
                continue;

            rt_kprintf("touch id=%u %s x=%u y=%u w=%u\n",
                       data[i].track_id,
                       touch_event_name(data[i].event),
                       data[i].x_coordinate,
                       data[i].y_coordinate,
                       data[i].width);
        }

        rt_thread_mdelay(50);
    }

    rt_device_close(dev);
}
MSH_CMD_EXPORT_ALIAS(cmd_touch_test, touch_test, read GT911 touch points);
