/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D21x I2C master driver.
 *
 * The ArtInChip HAL (vendor/hal/hal_i2c.c) implements the DesignWare
 * compatible I2C controller.  This adapter exposes it as an RT-Thread
 * I2C bus device.  Only master mode is wired; the vendor HAL retains the
 * slave primitives but the BSP does not register a slave interface.
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "hal_i2c.h"

#define AIC_I2C_STUCK_TIME  400

#ifndef BSP_I2C0_SPEED
#define BSP_I2C0_SPEED      400000
#endif
#ifndef BSP_I2C1_SPEED
#define BSP_I2C1_SPEED      400000
#endif
#ifndef BSP_I2C2_SPEED
#define BSP_I2C2_SPEED      400000
#endif
#ifndef BSP_I2C3_SPEED
#define BSP_I2C3_SPEED      400000
#endif
#ifndef BSP_I2C0_10BIT
#define BSP_I2C0_10BIT      0
#endif
#ifndef BSP_I2C1_10BIT
#define BSP_I2C1_10BIT      0
#endif
#ifndef BSP_I2C2_10BIT
#define BSP_I2C2_10BIT      0
#endif
#ifndef BSP_I2C3_10BIT
#define BSP_I2C3_10BIT      0
#endif

struct aic_i2c_bus
{
    struct rt_i2c_bus_device bus;
    aic_i2c_ctrl ctrl;
    const char *name;
};

static rt_ssize_t aic_i2c_master_xfer(struct rt_i2c_bus_device *bus,
                                      struct rt_i2c_msg msgs[],
                                      rt_uint32_t num)
{
    struct aic_i2c_bus *aic_bus = (struct aic_i2c_bus *)bus;
    aic_i2c_ctrl *ctrl = &aic_bus->ctrl;
    rt_uint32_t i;
    rt_uint32_t done = 0;

    for (i = 0; i < num; i++)
    {
        struct rt_i2c_msg *msg = &msgs[i];
        struct aic_i2c_msg hal_msg;
        rt_int32_t ret;

        hal_msg.addr = msg->addr;
        hal_msg.flags = msg->flags;
        hal_msg.len = msg->len;
        hal_msg.buf = msg->buf;

        if (msg->flags & RT_I2C_RD)
            ret = hal_i2c_master_receive_msg(ctrl, &hal_msg, (i == num - 1));
        else
            ret = hal_i2c_master_send_msg(ctrl, &hal_msg, (i == num - 1));

        if (ret < 0 || ret != msg->len)
            break;

        done++;
    }

    return done;
}

static rt_err_t aic_i2c_bus_control(struct rt_i2c_bus_device *bus,
                                    int cmd, void *args)
{
    struct aic_i2c_bus *aic_bus = (struct aic_i2c_bus *)bus;

    switch (cmd)
    {
    case RT_I2C_DEV_CTRL_CLK:
        aic_bus->ctrl.target_rate = (rt_uint32_t)(rt_ubase_t)args;
        return hal_i2c_set_speed(&aic_bus->ctrl) == I2C_OK ? RT_EOK : -RT_EIO;
    default:
        return -RT_EINVAL;
    }
}

static const struct rt_i2c_bus_device_ops aic_i2c_ops =
{
    .master_xfer = aic_i2c_master_xfer,
    .slave_xfer = RT_NULL,
    .i2c_bus_control = aic_i2c_bus_control,
};

#define AIC_I2C_BUS(n, base, irq, clk, speed, tenbit)                    \
    {                                                                    \
        .bus = { .ops = &aic_i2c_ops },                                  \
        .ctrl = {                                                        \
            .index = (n),                                                \
            .device_name = "i2c" #n,                                     \
            .reg_base = (base),                                          \
            .addr_bit = (tenbit),                                        \
            .target_rate = (speed),                                      \
            .bus_mode = 0,                                               \
            .slave_addr = 0,                                             \
            .irq_index = (irq),                                          \
            .clk_id = (clk),                                             \
            .stuck_time = AIC_I2C_STUCK_TIME,                            \
        },                                                               \
        .name = "i2c" #n,                                                \
    }

#ifdef BSP_USING_I2C0
static struct aic_i2c_bus aic_i2c0 = AIC_I2C_BUS(0, I2C0_BASE, I2C0_IRQn,
                                                 CLK_I2C0,
                                                 BSP_I2C0_SPEED,
                                                 BSP_I2C0_10BIT);
#endif
#ifdef BSP_USING_I2C1
static struct aic_i2c_bus aic_i2c1 = AIC_I2C_BUS(1, I2C1_BASE, I2C1_IRQn,
                                                 CLK_I2C1,
                                                 BSP_I2C1_SPEED,
                                                 BSP_I2C1_10BIT);
#endif
#ifdef BSP_USING_I2C2
static struct aic_i2c_bus aic_i2c2 = AIC_I2C_BUS(2, I2C2_BASE, I2C2_IRQn,
                                                 CLK_I2C2,
                                                 BSP_I2C2_SPEED,
                                                 BSP_I2C2_10BIT);
#endif
#ifdef BSP_USING_I2C3
static struct aic_i2c_bus aic_i2c3 = AIC_I2C_BUS(3, I2C3_BASE, I2C3_IRQn,
                                                 CLK_I2C3,
                                                 BSP_I2C3_SPEED,
                                                 BSP_I2C3_10BIT);
#endif

static int aic_i2c_register(struct aic_i2c_bus *bus)
{
    if (hal_i2c_init(&bus->ctrl) != I2C_OK)
    {
        rt_kprintf("%s: HAL init failed\n", bus->name);
        return -RT_ERROR;
    }

    return rt_i2c_bus_device_register(&bus->bus, bus->name);
}

int rt_hw_i2c_init(void)
{
    int ret = RT_EOK;

#ifdef BSP_USING_I2C0
    ret |= aic_i2c_register(&aic_i2c0);
#endif
#ifdef BSP_USING_I2C1
    ret |= aic_i2c_register(&aic_i2c1);
#endif
#ifdef BSP_USING_I2C2
    ret |= aic_i2c_register(&aic_i2c2);
#endif
#ifdef BSP_USING_I2C3
    ret |= aic_i2c_register(&aic_i2c3);
#endif

    return ret;
}
INIT_DEVICE_EXPORT(rt_hw_i2c_init);
