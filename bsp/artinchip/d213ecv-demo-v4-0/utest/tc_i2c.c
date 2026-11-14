/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "utest.h"

#ifdef BSP_USING_I2C3

#define TC_GT911_REG_PRODUCT_ID 0x8140
#define TC_GT911_ADDR_LOW       0x14
#define TC_GT911_ADDR_HIGH      0x5D

static int tc_gt911_product_id(rt_uint16_t addr, rt_uint8_t *id)
{
    struct rt_i2c_bus_device *bus;
    struct rt_i2c_msg msgs[2];
    rt_uint8_t reg[2];

    bus = (struct rt_i2c_bus_device *)rt_device_find("i2c3");
    if (bus == RT_NULL)
    {
        return 0;
    }

    reg[0] = (rt_uint8_t)(TC_GT911_REG_PRODUCT_ID >> 8);
    reg[1] = (rt_uint8_t)(TC_GT911_REG_PRODUCT_ID & 0xff);

    msgs[0].addr = addr;
    msgs[0].flags = RT_I2C_WR;
    msgs[0].buf = reg;
    msgs[0].len = sizeof(reg);

    msgs[1].addr = addr;
    msgs[1].flags = RT_I2C_RD;
    msgs[1].buf = id;
    msgs[1].len = 6;

    return (int)rt_i2c_transfer(bus, msgs, 2);
}

static void tc_i2c3_gt911(void)
{
    struct rt_i2c_bus_device *bus;
    rt_uint8_t id[6] = {0};
    int ret;

    bus = (struct rt_i2c_bus_device *)rt_device_find("i2c3");
    uassert_not_null(bus);
    if (bus == RT_NULL)
    {
        return;
    }

    ret = tc_gt911_product_id(TC_GT911_ADDR_HIGH, id);
    if (ret != 2)
    {
        ret = tc_gt911_product_id(TC_GT911_ADDR_LOW, id);
    }

    uassert_int_equal(ret, 2);
    if (ret == 2)
    {
        uassert_true(id[0] == 0x39 && id[1] == 0x31 && id[2] == 0x31);
        uassert_int_equal(id[3], 0x00);
    }
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_i2c3_gt911);
}
UTEST_TC_EXPORT(testcase, "bsp.d213.i2c3", RT_NULL, RT_NULL, 10);

#endif /* BSP_USING_I2C3 */
