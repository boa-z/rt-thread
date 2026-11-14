/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Small MSH validation commands for the D21x peripheral drivers.
 * These are intentionally thin: they exercise the standard RT-Thread
 * device interfaces and do not contain application or protocol logic.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <stdlib.h>
#include <string.h>

#if defined(BSP_USING_I2C0) || defined(BSP_USING_I2C1) || \
    defined(BSP_USING_I2C2) || defined(BSP_USING_I2C3)
static void i2c_scan_bus(const char *name)
{
    struct rt_i2c_bus_device *bus;
    struct rt_i2c_msg msg;
    rt_uint8_t byte;
    int addr;
    int found = 0;

    bus = (struct rt_i2c_bus_device *)rt_device_find(name);
    if (bus == RT_NULL)
    {
        rt_kprintf("%s not found\n", name);
        return;
    }

    rt_kprintf("scanning %s ...\n", name);
    for (addr = 0x08; addr <= 0x77; addr++)
    {
        msg.addr = addr;
        msg.flags = RT_I2C_RD;
        msg.len = 1;
        msg.buf = &byte;

        if (rt_i2c_transfer(bus, &msg, 1) == 1)
        {
            rt_kprintf("  found device at 0x%02x\n", addr);
            found++;
        }
    }
    rt_kprintf("%d device(s) found\n", found);
}

static void cmd_i2c_scan(int argc, char **argv)
{
    i2c_scan_bus((argc > 1) ? argv[1] : "i2c3");
}
MSH_CMD_EXPORT_ALIAS(cmd_i2c_scan, i2c_scan, scan an I2C bus);
#endif

#if defined(BSP_USING_CAN0) || defined(BSP_USING_CAN1)
static void cmd_can_send(int argc, char **argv)
{
    rt_device_t dev;
    struct rt_can_msg msg;
    int i;

    if (argc < 3)
    {
        rt_kprintf("usage: can_send <can0|can1> <id> [byte ...]\n");
        return;
    }

    dev = rt_device_find(argv[1]);
    if (dev == RT_NULL)
    {
        rt_kprintf("%s not found\n", argv[1]);
        return;
    }
    if (rt_device_open(dev, RT_DEVICE_FLAG_INT_RX | RT_DEVICE_FLAG_INT_TX) != RT_EOK)
    {
        rt_kprintf("open %s failed\n", argv[1]);
        return;
    }

    rt_memset(&msg, 0, sizeof(msg));
    msg.id = (rt_uint32_t)strtoul(argv[2], RT_NULL, 0);
    msg.ide = RT_CAN_STDID;
    msg.rtr = RT_CAN_DTR;
    msg.len = (argc > 3) ? (argc - 3) : 0;
    if (msg.len > 8)
        msg.len = 8;

    for (i = 0; i < msg.len; i++)
        msg.data[i] = (rt_uint8_t)strtoul(argv[3 + i], RT_NULL, 0);

    rt_kprintf("send id=0x%x len=%d -> %d bytes\n", msg.id, msg.len,
               (int)rt_device_write(dev, 0, &msg, sizeof(msg)));

    rt_device_close(dev);
}
MSH_CMD_EXPORT_ALIAS(cmd_can_send, can_send, send one classic CAN frame);

static struct rt_semaphore can_rx_sem;

static rt_err_t can_rx_ind(rt_device_t dev, rt_size_t size)
{
    (void)dev;
    (void)size;
    rt_sem_release(&can_rx_sem);
    return RT_EOK;
}

static void cmd_can_baud(int argc, char **argv)
{
    rt_device_t dev;
    rt_uint32_t baud;

    if (argc < 3)
    {
        rt_kprintf("usage: can_baud <can0|can1> <bitrate>\n");
        return;
    }

    dev = rt_device_find(argv[1]);
    if (dev == RT_NULL)
    {
        rt_kprintf("%s not found\n", argv[1]);
        return;
    }

    baud = (rt_uint32_t)strtoul(argv[2], RT_NULL, 0);
    if (baud == 0 || baud > 1000000)
    {
        rt_kprintf("D21x classic CAN supports 1..1000000 bit/s\n");
        return;
    }

    if (rt_device_open(dev, RT_DEVICE_FLAG_INT_RX) != RT_EOK)
    {
        rt_kprintf("open %s failed\n", argv[1]);
        return;
    }

    rt_kprintf("set %s bitrate %u -> %d\n", argv[1], baud,
               rt_device_control(dev, RT_CAN_CMD_SET_BAUD,
                                 (void *)(rt_ubase_t)baud));

    rt_device_close(dev);
}
MSH_CMD_EXPORT_ALIAS(cmd_can_baud, can_baud, set classic CAN bitrate);

static void cmd_can_mode(int argc, char **argv)
{
    rt_device_t dev;
    rt_uint32_t mode;

    if (argc < 3)
    {
        rt_kprintf("usage: can_mode <can0|can1> normal|listen|loopback\n");
        return;
    }

    if (!rt_strcmp(argv[2], "normal"))
        mode = RT_CAN_MODE_NORMAL;
    else if (!rt_strcmp(argv[2], "listen"))
        mode = RT_CAN_MODE_LISTEN;
    else if (!rt_strcmp(argv[2], "loopback"))
        mode = RT_CAN_MODE_LOOPBACK;
    else
    {
        rt_kprintf("unknown mode: %s\n", argv[2]);
        return;
    }

    dev = rt_device_find(argv[1]);
    if (dev == RT_NULL)
    {
        rt_kprintf("%s not found\n", argv[1]);
        return;
    }

    if (rt_device_open(dev, RT_DEVICE_FLAG_INT_RX) != RT_EOK)
    {
        rt_kprintf("open %s failed\n", argv[1]);
        return;
    }

    rt_kprintf("set %s mode %s -> %d\n", argv[1], argv[2],
               rt_device_control(dev, RT_CAN_CMD_SET_MODE,
                                 (void *)(rt_ubase_t)mode));

    rt_device_close(dev);
}
MSH_CMD_EXPORT_ALIAS(cmd_can_mode, can_mode, set classic CAN mode);

static void cmd_can_dump(int argc, char **argv)
{
    rt_device_t dev;
    struct rt_can_msg msg;
    int budget = (argc > 2) ? atoi(argv[2]) : 5;

    if (argc < 2)
    {
        rt_kprintf("usage: can_dump <can0|can1> [frame_count]\n");
        return;
    }

    dev = rt_device_find(argv[1]);
    if (dev == RT_NULL)
    {
        rt_kprintf("%s not found\n", argv[1]);
        return;
    }

    rt_sem_init(&can_rx_sem, "canrx", 0, RT_IPC_FLAG_FIFO);
    rt_device_set_rx_indicate(dev, can_rx_ind);
    if (rt_device_open(dev, RT_DEVICE_FLAG_INT_RX) != RT_EOK)
    {
        rt_kprintf("open %s failed\n", argv[1]);
        return;
    }

    rt_kprintf("waiting for %d frame(s) on %s ...\n", budget, argv[1]);
    while (budget-- > 0)
    {
        int i;

        if (rt_sem_take(&can_rx_sem, rt_tick_from_millisecond(3000)) != RT_EOK)
        {
            rt_kprintf("timeout\n");
            break;
        }

        while (rt_device_read(dev, 0, &msg, sizeof(msg)) == sizeof(msg))
        {
            rt_kprintf("id=0x%x ide=%d rtr=%d len=%d data=",
                       msg.id, msg.ide, msg.rtr, msg.len);
            for (i = 0; i < msg.len; i++)
                rt_kprintf("%02x ", msg.data[i]);
            rt_kprintf("\n");
        }
    }

    rt_device_close(dev);
    rt_sem_detach(&can_rx_sem);
}
MSH_CMD_EXPORT_ALIAS(cmd_can_dump, can_dump, receive classic CAN frames);
#endif

#ifdef BSP_USING_PWM
static void cmd_pwm_test(int argc, char **argv)
{
    struct rt_device_pwm *pwm;
    rt_uint32_t channel;
    rt_uint32_t period;
    rt_uint32_t pulse;
    int seconds;

    if (argc < 5)
    {
        rt_kprintf("usage: pwm_test <ch> <period_ns> <pulse_ns> <seconds>\n");
        return;
    }

    pwm = (struct rt_device_pwm *)rt_device_find("pwm");
    if (pwm == RT_NULL)
    {
        rt_kprintf("pwm not found\n");
        return;
    }

    channel = atoi(argv[1]);
    period = strtoul(argv[2], RT_NULL, 0);
    pulse = strtoul(argv[3], RT_NULL, 0);
    seconds = atoi(argv[4]);

    if (rt_pwm_set(pwm, channel, period, pulse) != RT_EOK ||
        rt_pwm_enable(pwm, channel) != RT_EOK)
    {
        rt_kprintf("pwm configuration failed\n");
        return;
    }

    rt_kprintf("pwm ch%u %u/%u ns for %d s\n", channel, pulse, period, seconds);
    rt_thread_mdelay(seconds * 1000);
    rt_pwm_disable(pwm, channel);
}
MSH_CMD_EXPORT_ALIAS(cmd_pwm_test, pwm_test, drive a PWM channel temporarily);
#endif

#ifdef BSP_USING_GPIO
static void cmd_gpio_toggle(int argc, char **argv)
{
    rt_base_t pin;
    int count;
    int delay;

    if (argc < 4)
    {
        rt_kprintf("usage: gpio_toggle <PA.0> <count> <delay_ms>\n");
        return;
    }

    pin = rt_pin_get(argv[1]);
    if (pin < 0)
    {
        rt_kprintf("bad pin name: %s\n", argv[1]);
        return;
    }

    count = atoi(argv[2]);
    delay = atoi(argv[3]);

    rt_pin_mode(pin, PIN_MODE_OUTPUT);
    while (count-- > 0)
    {
        rt_pin_write(pin, PIN_HIGH);
        rt_thread_mdelay(delay);
        rt_pin_write(pin, PIN_LOW);
        rt_thread_mdelay(delay);
    }
    rt_kprintf("gpio %s toggled\n", argv[1]);
}
MSH_CMD_EXPORT_ALIAS(cmd_gpio_toggle, gpio_toggle, toggle a GPIO pin);
#endif

#ifdef BSP_USING_RTC
static void cmd_rtc_test(int argc, char **argv)
{
    rt_device_t dev;
    time_t now = 0;
    struct tm tm;

    (void)argc;
    (void)argv;

    dev = rt_device_find("rtc");
    if (dev == RT_NULL)
    {
        rt_kprintf("rtc not found\n");
        return;
    }
    if (rt_device_open(dev, RT_DEVICE_FLAG_RDWR) != RT_EOK)
    {
        rt_kprintf("open rtc failed\n");
        return;
    }

    if (rt_device_control(dev, RT_DEVICE_CTRL_RTC_GET_TIME, &now) == RT_EOK)
    {
        gmtime_r(&now, &tm);
        rt_kprintf("RTC: %04d-%02d-%02d %02d:%02d:%02d (epoch %ld)\n",
                   tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                   tm.tm_hour, tm.tm_min, tm.tm_sec, (long)now);
    }
    else
    {
        rt_kprintf("RTC read failed\n");
    }

    rt_device_close(dev);
}
MSH_CMD_EXPORT_ALIAS(cmd_rtc_test, rtc_test, read the RTC);
#endif

#ifdef BSP_USING_WDT
static void cmd_wdt_test(int argc, char **argv)
{
    rt_device_t dev;
    const char *action = (argc > 1) ? argv[1] : "feed";

    dev = rt_device_find("wdt");
    if (dev == RT_NULL)
    {
        rt_kprintf("wdt not found\n");
        return;
    }
    if (rt_device_open(dev, RT_DEVICE_FLAG_RDWR) != RT_EOK)
    {
        rt_kprintf("open wdt failed\n");
        return;
    }

    if (!rt_strcmp(action, "start"))
    {
        rt_uint32_t timeout = (argc > 2) ? (rt_uint32_t)atoi(argv[2]) : 10;
        rt_device_control(dev, RT_DEVICE_CTRL_WDT_SET_TIMEOUT, &timeout);
        rt_device_control(dev, RT_DEVICE_CTRL_WDT_START, RT_NULL);
        rt_kprintf("watchdog started, timeout %u s; feed it or the board resets\n",
                   timeout);
    }
    else if (!rt_strcmp(action, "stop"))
    {
        rt_device_control(dev, RT_DEVICE_CTRL_WDT_STOP, RT_NULL);
        rt_kprintf("watchdog stopped\n");
    }
    else if (!rt_strcmp(action, "timeleft"))
    {
        rt_uint32_t left = 0;

        rt_device_control(dev, RT_DEVICE_CTRL_WDT_GET_TIMELEFT, &left);
        rt_kprintf("watchdog time left: %u s\n", left);
    }
    else
    {
        rt_device_control(dev, RT_DEVICE_CTRL_WDT_KEEPALIVE, RT_NULL);
        rt_kprintf("watchdog fed\n");
    }

    /* Keep the device open: closing would stop the watchdog. */
}
MSH_CMD_EXPORT_ALIAS(cmd_wdt_test, wdt_test, start/stop/feed/timeleft the watchdog);
#endif

static void cmd_periph_status(int argc, char **argv)
{
    static const char *names[] =
    {
#ifdef BSP_USING_UART0
        "uart0",
#endif
#ifdef BSP_USING_UART1
        "uart1",
#endif
#ifdef BSP_USING_UART2
        "uart2",
#endif
#ifdef BSP_USING_UART4
        "uart4",
#endif
#ifdef BSP_USING_I2C0
        "i2c0",
#endif
#ifdef BSP_USING_I2C1
        "i2c1",
#endif
#ifdef BSP_USING_I2C2
        "i2c2",
#endif
#ifdef BSP_USING_I2C3
        "i2c3",
#endif
#ifdef BSP_USING_CAN0
        "can0",
#endif
#ifdef BSP_USING_CAN1
        "can1",
#endif
#ifdef BSP_USING_PWM
        "pwm",
#endif
#ifdef BSP_USING_WDT
        "wdt",
#endif
#ifdef BSP_USING_RTC
        "rtc",
#endif
#ifdef BSP_USING_GPIO
        "gpio",
#endif
#ifdef BSP_USING_TOUCH_GT911
        "gt911",
#endif
    };
    rt_size_t i;

    (void)argc;
    (void)argv;

    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        rt_kprintf("%-6s %s\n", names[i],
                   rt_device_find(names[i]) ? "registered" : "missing");
}
MSH_CMD_EXPORT_ALIAS(cmd_periph_status, periph_status, list registered BSP devices);
