/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D21x classic CAN 2.0A/B driver.
 *
 * The ArtInChip HAL (vendor/hal/aic_hal_can.c) implements the D21x CAN
 * controller: standard and extended identifiers, remote frames, a
 * two-filter hardware acceptance block, listen/loopback modes, error
 * counters and bus-off detection.  There is NO CAN FD support in this
 * controller and none is exposed here.
 *
 * This adapter follows the vendor driver's structure but targets the
 * RT-Thread 5.3 CAN device operations.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <interrupt.h>
#include <string.h>

#include "aic_hal_can.h"

struct aic_can
{
    struct rt_can_device can;
    can_handle handle;
    const char *name;
    rt_uint32_t index;
    rt_bool_t single_shot;
};

static rt_err_t aic_can_configure(struct rt_can_device *can,
                                  struct can_configure *cfg)
{
    struct aic_can *aic = (struct aic_can *)can;

    if (cfg->baud_rate == 0)
        return -RT_EINVAL;

    aic->handle.baudrate = cfg->baud_rate;
    hal_can_ioctl(&aic->handle, CAN_IOCTL_SET_BAUDRATE,
                  (void *)(rt_ubase_t)cfg->baud_rate);

    return RT_EOK;
}

static int aic_can_filter_to_hw(struct rt_can_filter_config *filter,
                                can_filter_config_t *hw)
{
    rt_uint32_t count;

    rt_memset(hw, 0, sizeof(*hw));
    count = filter->count;
    if (count > 2)
        count = 2;

    if (count == 0)
    {
        /* Disable acceptance filtering: receive everything. */
        hw->filter_mode = SINGLE_FILTER_MODE;
        hw->is_eff = 0;
        hw->rxmask.sfs.id_filter = 0xFFFF;
        hw->rxmask.sfs.rtr_filter = 0xF;
        hw->rxmask.sfs.data0_filter = 0xFF;
        hw->rxmask.sfs.data1_filter = 0xFF;
        return 0;
    }

    if (filter->items == RT_NULL)
        return -RT_EINVAL;

    hw->filter_mode = (count == 1) ? SINGLE_FILTER_MODE : DUAL_FILTER_MODE;
    hw->is_eff = filter->items[0].ide;

    if (count == 1)
    {
        if (hw->is_eff)
        {
            hw->rxcode.sfe.id_filter = filter->items[0].id & filter->items[0].mask;
            hw->rxmask.sfe.id_filter = ~filter->items[0].mask;
            hw->rxcode.sfe.rtr_filter = filter->items[0].rtr;
        }
        else
        {
            hw->rxcode.sfs.id_filter = filter->items[0].id & filter->items[0].mask;
            hw->rxmask.sfs.id_filter = ~filter->items[0].mask;
            hw->rxcode.sfs.rtr_filter = filter->items[0].rtr;
            hw->rxmask.sfs.data0_filter = 0xFF;
            hw->rxmask.sfs.data1_filter = 0xFF;
        }
        return 0;
    }

    if (filter->items[0].ide != filter->items[1].ide)
        return -RT_EINVAL;

    if (hw->is_eff)
    {
        /* The dual extended filter only compares ID[28:13]. */
        hw->rxcode.dfe.id_filter0 =
            (filter->items[0].id & filter->items[0].mask) >> 13;
        hw->rxcode.dfe.id_filter1 =
            (filter->items[1].id & filter->items[1].mask) >> 13;
        hw->rxmask.dfe.id_filter0 = ~filter->items[0].mask >> 13;
        hw->rxmask.dfe.id_filter1 = ~filter->items[1].mask >> 13;
    }
    else
    {
        hw->rxcode.dfs.id_filter0 = filter->items[0].id & filter->items[0].mask;
        hw->rxcode.dfs.id_filter1 = filter->items[1].id & filter->items[1].mask;
        hw->rxmask.dfs.id_filter0 = ~filter->items[0].mask;
        hw->rxmask.dfs.id_filter1 = ~filter->items[1].mask;
        hw->rxcode.dfs.rtr_filter0 = filter->items[0].rtr;
        hw->rxcode.dfs.rtr_filter1 = filter->items[1].rtr;
        hw->rxmask.dfs.data0_filter0 = 0xFF;
    }

    return 0;
}

static rt_err_t aic_can_control(struct rt_can_device *can, int cmd, void *arg)
{
    struct aic_can *aic = (struct aic_can *)can;
    struct rt_can_status *status;

    switch (cmd)
    {
    case RT_DEVICE_CTRL_SET_INT:
        hal_can_enable_interrupt(&aic->handle);
        rt_hw_interrupt_umask(aic->handle.irq_num);
        break;

    case RT_DEVICE_CTRL_CLR_INT:
        hal_can_disable_interrupt(&aic->handle);
        rt_hw_interrupt_mask(aic->handle.irq_num);
        break;

    case RT_CAN_CMD_SET_BAUD:
        aic->handle.baudrate = (rt_uint32_t)(rt_ubase_t)arg;
        hal_can_ioctl(&aic->handle, CAN_IOCTL_SET_BAUDRATE, arg);
        break;

    case RT_CAN_CMD_SET_MODE:
        aic->handle.mode = (unsigned long)(rt_ubase_t)arg;
        hal_can_ioctl(&aic->handle, CAN_IOCTL_SET_MODE,
                      (void *)(rt_ubase_t)arg);
        break;

    case RT_CAN_CMD_SET_FILTER:
    {
        can_filter_config_t hw;
        struct rt_can_filter_config *filter =
            (struct rt_can_filter_config *)arg;

        if (filter == RT_NULL)
            return -RT_EINVAL;

        if (aic_can_filter_to_hw(filter, &hw) < 0)
            return -RT_EINVAL;

        hal_can_ioctl(&aic->handle, CAN_IOCTL_SET_FILTER, &hw);
        break;
    }

    case RT_CAN_CMD_GET_STATUS:
        status = (struct rt_can_status *)arg;
        if (status == RT_NULL)
            return -RT_EINVAL;

        status->rcvpkg = can->status.rcvpkg;
        status->dropedrcvpkg = can->status.dropedrcvpkg;
        status->sndpkg = can->status.sndpkg;
        status->dropedsndpkg = can->status.dropedsndpkg;
        status->rcverrcnt = aic->handle.status.recverrcnt;
        status->snderrcnt = aic->handle.status.snderrcnt;
        status->bitpaderrcnt = aic->handle.status.stufferrcnt;
        status->formaterrcnt = aic->handle.status.formaterrcnt;
        status->biterrcnt = aic->handle.status.biterrcnt;
        status->ackerrcnt = aic->handle.status.ackerrcnt;
        status->crcerrcnt = aic->handle.status.crcerrcnt;
        status->errcode = (rt_uint32_t)aic->handle.status.current_state;
        break;

    case RT_CAN_CMD_START:
        /* The controller starts when leaving reset mode during init. */
        break;

    default:
        return -RT_EINVAL;
    }

    return RT_EOK;
}

static rt_ssize_t aic_can_sendmsg(struct rt_can_device *can,
                                  const void *buf, rt_uint32_t boxno)
{
    struct aic_can *aic = (struct aic_can *)can;
    const struct rt_can_msg *msg = (const struct rt_can_msg *)buf;
    can_msg_t frame;
    can_op_req_t req = TX_REQ;
    int ret;
    int i;

    (void)boxno;

    if (msg->len > 8)
        return -RT_EINVAL;

    rt_memset(&frame, 0, sizeof(frame));
    frame.id = msg->id;
    frame.ide = msg->ide;
    frame.rtr = msg->rtr;
    frame.dlc = msg->len;
    for (i = 0; i < msg->len; i++)
        frame.data[i] = msg->data[i];

    switch (aic->handle.mode)
    {
    case RT_CAN_MODE_LOOPBACK:
        req = SELF_REQ;
        break;
    case RT_CAN_MODE_NORMAL:
        if (aic->single_shot)
            req = SINGLE_TX_REQ;
        break;
    case RT_CAN_MODE_LISTEN:
    case RT_CAN_MODE_LOOPBACKANLISTEN:
    default:
        return -RT_EBUSY;
    }

    ret = hal_can_send_frame(&aic->handle, &frame, req);
    return (ret == 0) ? RT_EOK : (rt_ssize_t)ret;
}

static rt_ssize_t aic_can_recvmsg(struct rt_can_device *can,
                                  void *buf, rt_uint32_t fifo)
{
    struct aic_can *aic = (struct aic_can *)can;
    struct rt_can_msg *msg = (struct rt_can_msg *)buf;
    int i;

    (void)fifo;

    msg->id = aic->handle.msg.id;
    msg->ide = aic->handle.msg.ide;
    msg->rtr = aic->handle.msg.rtr;
    msg->len = aic->handle.msg.dlc;
    msg->hdr_index = -1;
    for (i = 0; i < msg->len && i < 8; i++)
        msg->data[i] = aic->handle.msg.data[i];

    return 0;
}

static const struct rt_can_ops aic_can_ops =
{
    .configure = aic_can_configure,
    .control = aic_can_control,
    .sendmsg = aic_can_sendmsg,
    .recvmsg = aic_can_recvmsg,
    .sendmsg_nonblocking = RT_NULL,
};

static void aic_can_callback(can_handle *handle, void *arg)
{
    struct aic_can *aic = rt_container_of(handle, struct aic_can, handle);
    unsigned long event = (unsigned long)arg;

    switch (event)
    {
    case CAN_EVENT_RX_IND:
        rt_hw_can_isr(&aic->can, RT_CAN_EVENT_RX_IND);
        break;
    case CAN_EVENT_TX_DONE:
        rt_hw_can_isr(&aic->can, RT_CAN_EVENT_TX_DONE);
        break;
    case CAN_EVENT_TX_FAIL:
        rt_hw_can_isr(&aic->can, RT_CAN_EVENT_TX_FAIL);
        break;
    case CAN_EVENT_RXOF_IND:
        rt_hw_can_isr(&aic->can, RT_CAN_EVENT_RXOF_IND);
        break;
    default:
        break;
    }
}

static void aic_can_isr(int irq, void *param)
{
    hal_can_isr_handler(irq, param);
}

static int aic_can_register(struct aic_can *aic)
{
    struct can_configure config = CANDEFAULTCONFIG;

    config.privmode = RT_CAN_MODE_NOPRIV;
    config.ticks = 50;

    if (hal_can_init(&aic->handle, aic->index) < 0)
    {
        rt_kprintf("%s: HAL init failed\n", aic->name);
        return -RT_ERROR;
    }

    /* Translate the SoC PLIC hardware IRQ to the RT-Thread handler index. */
    aic->handle.irq_num = IRQ_PLIC(aic->handle.irq_num);

    aic->handle.autoBusoff = 1;
    hal_can_attach_callback(&aic->handle, aic_can_callback, RT_NULL);

    aic->can.config = config;
    rt_hw_interrupt_install(aic->handle.irq_num, aic_can_isr,
                            &aic->handle, aic->name);
    rt_hw_interrupt_umask(aic->handle.irq_num);

    return rt_hw_can_register(&aic->can, aic->name, &aic_can_ops, RT_NULL);
}

#ifdef BSP_USING_CAN0
static struct aic_can aic_can0 = { .name = "can0", .index = 0 };
#endif
#ifdef BSP_USING_CAN1
static struct aic_can aic_can1 = { .name = "can1", .index = 1 };
#endif

int rt_hw_can_init(void)
{
    int ret = RT_EOK;

#ifdef BSP_USING_CAN0
    ret |= aic_can_register(&aic_can0);
#endif
#ifdef BSP_USING_CAN1
    ret |= aic_can_register(&aic_can1);
#endif

    return ret;
}
INIT_DEVICE_EXPORT(rt_hw_can_init);
