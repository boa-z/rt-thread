/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * GT911 capacitive touch adapter.
 *
 * Wiring, I2C bus and fallback coordinate ranges come from aic_board_touch.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include "aic_board.h"

#define DBG_TAG "gt911"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define GT911_ADDR_LOW            0x14
#define GT911_ADDR_HIGH           0x5D
#define GT911_MAX_TOUCH           5
#define GT911_POINT_INFO_NUM      8
#define GT911_REG_LEN             2

#define GT911_PRODUCT_ID          0x8140
#define GT911_CONFIG_REG          0x8047
#define GT911_READ_STATUS         0x814E
#define GT911_POINT1_REG          0x814F

static struct rt_i2c_client gt911_client;
static rt_base_t gt911_rst_pin = -1;
static rt_base_t gt911_int_pin = -1;
static rt_uint8_t gt911_addr = GT911_ADDR_LOW;
static rt_uint16_t gt911_last_x[GT911_MAX_TOUCH];
static rt_uint16_t gt911_last_y[GT911_MAX_TOUCH];
static rt_uint8_t gt911_last_w[GT911_MAX_TOUCH];
static rt_uint8_t gt911_pressed[GT911_MAX_TOUCH];

static rt_err_t gt911_write_regs(const rt_uint8_t *data, rt_uint16_t len)
{
    struct rt_i2c_msg msg;

    msg.addr = gt911_client.client_addr;
    msg.flags = RT_I2C_WR;
    msg.buf = (rt_uint8_t *)data;
    msg.len = len;

    return (rt_i2c_transfer(gt911_client.bus, &msg, 1) == 1) ?
           RT_EOK : -RT_EIO;
}

static rt_err_t gt911_read_regs(rt_uint16_t reg, rt_uint8_t *data,
                                rt_uint16_t len)
{
    rt_uint8_t regbuf[GT911_REG_LEN];
    struct rt_i2c_msg msgs[2];

    regbuf[0] = (rt_uint8_t)(reg >> 8);
    regbuf[1] = (rt_uint8_t)(reg & 0xFF);

    msgs[0].addr = gt911_client.client_addr;
    msgs[0].flags = RT_I2C_WR;
    msgs[0].buf = regbuf;
    msgs[0].len = GT911_REG_LEN;

    msgs[1].addr = gt911_client.client_addr;
    msgs[1].flags = RT_I2C_RD;
    msgs[1].buf = data;
    msgs[1].len = len;

    return (rt_i2c_transfer(gt911_client.bus, msgs, 2) == 2) ?
           RT_EOK : -RT_EIO;
}

static rt_err_t gt911_read_product_id(rt_uint8_t addr, rt_uint8_t *data,
                                      rt_uint16_t len)
{
    gt911_client.client_addr = addr;
    if (gt911_read_regs(GT911_PRODUCT_ID, data, len) != RT_EOK)
        return -RT_EIO;

    if (data[0] == 0x00 || data[0] == 0xFF)
        return -RT_EIO;

    return RT_EOK;
}

static rt_uint8_t gt911_probe(void)
{
    char id[7] = {0};
    rt_uint8_t addrs[2] = {GT911_ADDR_LOW, GT911_ADDR_HIGH};
    int i;

    for (i = 0; i < 2; i++)
    {
        gt911_client.client_addr = addrs[i];
        if (gt911_read_product_id(addrs[i], (rt_uint8_t *)id, 6) == RT_EOK)
        {
            rt_kprintf("gt911: found at 0x%02x, id %s\n", addrs[i], id);
            return addrs[i];
        }
    }

    return 0;
}

static void gt911_reset(void)
{
    if (gt911_rst_pin < 0 || gt911_int_pin < 0)
        return;

    /* Follow the vendor reset sequence used by the D21x demo128 BSP. */
    rt_pin_mode(gt911_rst_pin, PIN_MODE_OUTPUT);
    rt_pin_write(gt911_rst_pin, PIN_LOW);
    rt_thread_mdelay(10);

    rt_pin_mode(gt911_int_pin, PIN_MODE_OUTPUT);
    rt_pin_write(gt911_int_pin, PIN_LOW);
    rt_thread_mdelay(2);

    rt_pin_write(gt911_rst_pin, PIN_HIGH);
    rt_thread_mdelay(5);
    rt_pin_mode(gt911_rst_pin, PIN_MODE_INPUT);

    rt_pin_mode(gt911_int_pin, PIN_MODE_OUTPUT);
    rt_pin_write(gt911_int_pin, PIN_LOW);
    rt_thread_mdelay(50);

    rt_pin_mode(gt911_int_pin, PIN_MODE_INPUT);
    rt_thread_mdelay(100);
}

static rt_err_t gt911_get_info(struct rt_touch_info *info)
{
    rt_uint8_t cfg[7];

    if (gt911_read_regs(GT911_CONFIG_REG, cfg, sizeof(cfg)) != RT_EOK)
        return -RT_EIO;

    info->range_x = aic_board_touch.x_range;
    info->range_y = aic_board_touch.y_range;
    info->point_num = cfg[5] & 0x0F;
    if (info->point_num == 0 || info->point_num > GT911_MAX_TOUCH)
        info->point_num = GT911_MAX_TOUCH;

    return RT_EOK;
}

static void gt911_touch_event(struct rt_touch_data *data, rt_uint8_t id,
                              rt_uint8_t event, rt_int16_t x, rt_int16_t y,
                              rt_int16_t width)
{
    data[id].event = event;
    data[id].track_id = id;
    data[id].width = (rt_uint8_t)width;
    data[id].x_coordinate = (rt_uint16_t)x;
    data[id].y_coordinate = (rt_uint16_t)y;
    data[id].timestamp = rt_touch_get_ts();
}

static rt_size_t gt911_read_point(struct rt_touch_device *touch, void *buf,
                                  rt_size_t read_num)
{
    struct rt_touch_data *data = (struct rt_touch_data *)buf;
    rt_uint8_t points[GT911_POINT_INFO_NUM * GT911_MAX_TOUCH] = {0};
    rt_uint8_t down[GT911_MAX_TOUCH] = {0};
    rt_uint8_t clear[3];
    rt_uint8_t status;
    rt_uint8_t count;
    rt_uint8_t i;

    (void)touch;

    if (read_num > GT911_MAX_TOUCH)
        read_num = GT911_MAX_TOUCH;
    if (read_num == 0)
        return 0;

    rt_memset(data, 0, sizeof(struct rt_touch_data) * read_num);

    if (gt911_read_regs(GT911_READ_STATUS, &status, 1) != RT_EOK)
        return 0;

    if ((status & 0x80) == 0)
        return 0;

    count = status & 0x0F;
    if (count > GT911_MAX_TOUCH)
        count = GT911_MAX_TOUCH;

    if (count > 0 &&
        gt911_read_regs(GT911_POINT1_REG, points,
                        count * GT911_POINT_INFO_NUM) != RT_EOK)
    {
        count = 0;
    }

    for (i = 0; i < count; i++)
    {
        rt_uint8_t off = i * GT911_POINT_INFO_NUM;
        rt_uint8_t id = points[off] & 0x0F;
        rt_int16_t x;
        rt_int16_t y;
        rt_int16_t w;

        if (id >= read_num)
            continue;

        x = points[off + 1] | (points[off + 2] << 8);
        y = points[off + 3] | (points[off + 4] << 8);
        w = points[off + 5] | (points[off + 6] << 8);

        gt911_touch_event(data, id,
                          gt911_pressed[id] ? RT_TOUCH_EVENT_MOVE :
                          RT_TOUCH_EVENT_DOWN, x, y, w);
        gt911_last_x[id] = (rt_uint16_t)x;
        gt911_last_y[id] = (rt_uint16_t)y;
        gt911_last_w[id] = (rt_uint8_t)w;
        gt911_pressed[id] = 1;
        down[id] = 1;
    }

    /* The controller reports count 0 for the final release. */
    for (i = 0; i < read_num; i++)
    {
        if (!down[i] && gt911_pressed[i])
        {
            gt911_pressed[i] = 0;
            gt911_touch_event(data, i, RT_TOUCH_EVENT_UP,
                              gt911_last_x[i], gt911_last_y[i],
                              gt911_last_w[i]);
        }
    }

    /* Clear the status register so the controller can report the next sample. */
    clear[0] = (rt_uint8_t)(GT911_READ_STATUS >> 8);
    clear[1] = (rt_uint8_t)(GT911_READ_STATUS & 0xFF);
    clear[2] = 0;
    gt911_write_regs(clear, sizeof(clear));

    return read_num;
}

static rt_err_t gt911_control(struct rt_touch_device *touch, int cmd, void *arg)
{
    switch (cmd)
    {
    case RT_TOUCH_CTRL_GET_ID:
        return gt911_read_product_id(gt911_addr, (rt_uint8_t *)arg, 6);

    case RT_TOUCH_CTRL_GET_INFO:
        return gt911_get_info((struct rt_touch_info *)arg);

    case RT_TOUCH_CTRL_POWER_OFF:
        if (gt911_rst_pin >= 0)
            rt_pin_write(gt911_rst_pin, PIN_LOW);
        return RT_EOK;

    case RT_TOUCH_CTRL_POWER_ON:
        gt911_reset();
        return RT_EOK;

    default:
        return RT_EOK;
    }
}

static const struct rt_touch_ops gt911_touch_ops =
{
    .touch_readpoint = gt911_read_point,
    .touch_control = gt911_control,
};

static int rt_hw_gt911_init(void)
{
    struct rt_touch_config cfg = {0};
    struct rt_touch_device *touch;

    if (!aic_board_touch.reset_pin || !aic_board_touch.interrupt_pin ||
        !aic_board_touch.i2c_bus || !aic_board_touch.i2c_bus[0] ||
        !aic_board_touch.x_range || aic_board_touch.x_range > 65535 ||
        !aic_board_touch.y_range || aic_board_touch.y_range > 65535)
        return -RT_EINVAL;

    gt911_rst_pin = rt_pin_get(aic_board_touch.reset_pin);
    gt911_int_pin = rt_pin_get(aic_board_touch.interrupt_pin);
    if (gt911_rst_pin < 0 || gt911_int_pin < 0)
    {
        LOG_E("invalid reset/interrupt pin");
        return -RT_EINVAL;
    }

    gt911_reset();

    gt911_client.bus =
        (struct rt_i2c_bus_device *)rt_device_find(aic_board_touch.i2c_bus);
    if (gt911_client.bus == RT_NULL)
    {
        LOG_E("can't find %s", aic_board_touch.i2c_bus);
        return -RT_ERROR;
    }

    if (rt_device_open((rt_device_t)gt911_client.bus,
                       RT_DEVICE_FLAG_RDWR) != RT_EOK)
    {
        LOG_E("open %s failed", aic_board_touch.i2c_bus);
        return -RT_ERROR;
    }

    gt911_addr = gt911_probe();
    if (gt911_addr == 0)
    {
        LOG_E("GT911 not found");
        return -RT_ERROR;
    }

    touch = (struct rt_touch_device *)rt_calloc(1, sizeof(*touch));
    if (touch == RT_NULL)
        return -RT_ENOMEM;

    touch->info.type = RT_TOUCH_TYPE_CAPACITANCE;
    touch->info.vendor = RT_TOUCH_VENDOR_GT;
    if (gt911_get_info(&touch->info) != RT_EOK)
    {
        touch->info.range_x = aic_board_touch.x_range;
        touch->info.range_y = aic_board_touch.y_range;
        touch->info.point_num = GT911_MAX_TOUCH;
    }

    /* The Touch API predates const-qualified device names. */
    cfg.dev_name = (char *)aic_board_touch.i2c_bus;
    cfg.irq_pin.pin = gt911_int_pin;
    cfg.irq_pin.mode = PIN_MODE_INPUT;
    rt_memcpy(&touch->config, &cfg, sizeof(cfg));
    touch->ops = &gt911_touch_ops;

    if (rt_hw_touch_register(touch, "gt911",
                             RT_DEVICE_FLAG_INT_RX, RT_NULL) != RT_EOK)
    {
        LOG_E("register gt911 failed");
        return -RT_ERROR;
    }

    LOG_I("GT911 registered on %s at 0x%02x", aic_board_touch.i2c_bus,
          gt911_addr);
    return RT_EOK;
}
INIT_DEVICE_EXPORT(rt_hw_gt911_init);

static void gt911_print_hex(const char *name, const rt_uint8_t *data,
                            rt_uint16_t len)
{
    rt_uint16_t i;

    rt_kprintf("%s:", name);
    for (i = 0; i < len; i++)
        rt_kprintf(" %02x", data[i]);
    rt_kprintf("\n");
}

static void cmd_gt911_dump(void)
{
    rt_uint8_t id[6] = {0};
    rt_uint8_t cfg[8] = {0};
    rt_uint8_t point[8] = {0};
    rt_uint8_t status = 0;

    if (gt911_client.bus == RT_NULL)
    {
        rt_kprintf("gt911 not initialized\n");
        return;
    }

    if (gt911_read_product_id(gt911_addr, id, sizeof(id)) != RT_EOK)
    {
        rt_kprintf("gt911 id read failed at 0x%02x\n", gt911_addr);
        return;
    }

    rt_kprintf("gt911: addr 0x%02x\n", gt911_addr);
    gt911_print_hex("id", id, sizeof(id));
    if (gt911_read_regs(GT911_CONFIG_REG, cfg, sizeof(cfg)) == RT_EOK)
        gt911_print_hex("cfg", cfg, sizeof(cfg));
    if (gt911_read_regs(GT911_READ_STATUS, &status, 1) == RT_EOK)
    {
        rt_kprintf("status: 0x%02x, count %u\n", status, status & 0x0F);
        if ((status & 0x80) &&
            gt911_read_regs(GT911_POINT1_REG, point, sizeof(point)) == RT_EOK)
        {
            gt911_print_hex("point1", point, sizeof(point));
        }
    }
}
MSH_CMD_EXPORT_ALIAS(cmd_gt911_dump, gt911_dump, dump GT911 registers);

static void cmd_gt911_reset(void)
{
    if (gt911_client.bus == RT_NULL)
    {
        rt_kprintf("gt911 not initialized\n");
        return;
    }

    gt911_reset();
    gt911_addr = gt911_probe();
    if (gt911_addr == 0)
        rt_kprintf("gt911 not found after reset\n");
}
MSH_CMD_EXPORT_ALIAS(cmd_gt911_reset, gt911_reset, reset and probe GT911);
