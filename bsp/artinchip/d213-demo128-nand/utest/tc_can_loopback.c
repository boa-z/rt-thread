/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Optional CAN0 SELFTEST loopback.  On D21x this mode needs CAN0 TX/RX
 * wired together externally, or a transceiver plus a second ACK-ing node;
 * the controller alone did not receive or complete a frame on the
 * demo128 board without that wiring.  Enable BSP_UTEST_CAN_LOOPBACK only
 * when the board is prepared accordingly.
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "utest.h"

#if defined(BSP_USING_CAN0) && defined(BSP_UTEST_CAN_LOOPBACK)

#define TC_CAN_ID        0x123
#define TC_CAN_BAUD      500000
#define TC_CAN_RX_WAIT   1000
#define TC_CAN_DATA0     0x11
#define TC_CAN_DATA1     0x22
#define TC_CAN_DATA2     0x33

static struct rt_semaphore tc_can_rx_sem;

static rt_err_t tc_can_rx_indicate(rt_device_t dev, rt_size_t size)
{
    (void)dev;
    (void)size;
    rt_sem_release(&tc_can_rx_sem);
    return RT_EOK;
}

static void tc_can_loopback(void)
{
    rt_device_t dev;
    struct rt_can_msg tx_msg;
    struct rt_can_msg rx_msg;
    struct rt_can_status status;
    rt_err_t ret;
    rt_err_t rx_ret;
    rt_ssize_t written;

    dev = rt_device_find("can0");
    uassert_not_null(dev);
    if (dev == RT_NULL)
    {
        return;
    }

    rt_sem_init(&tc_can_rx_sem, "ut_can", 0, RT_IPC_FLAG_FIFO);

    ret = rt_device_open(dev, RT_DEVICE_FLAG_INT_RX | RT_DEVICE_FLAG_INT_TX);
    uassert_int_equal(ret, RT_EOK);
    if (ret != RT_EOK)
    {
        rt_sem_detach(&tc_can_rx_sem);
        return;
    }

    rt_device_set_rx_indicate(dev, tc_can_rx_indicate);

    uassert_int_equal(rt_device_control(dev, RT_CAN_CMD_SET_BAUD,
                                        (void *)(rt_ubase_t)TC_CAN_BAUD),
                      RT_EOK);
    uassert_int_equal(rt_device_control(dev, RT_CAN_CMD_SET_MODE,
                                        (void *)(rt_ubase_t)RT_CAN_MODE_LOOPBACK),
                      RT_EOK);

    rt_memset(&tx_msg, 0, sizeof(tx_msg));
    tx_msg.id = TC_CAN_ID;
    tx_msg.ide = RT_CAN_STDID;
    tx_msg.rtr = RT_CAN_DTR;
    tx_msg.len = 3;
    tx_msg.data[0] = TC_CAN_DATA0;
    tx_msg.data[1] = TC_CAN_DATA1;
    tx_msg.data[2] = TC_CAN_DATA2;

    written = rt_device_write(dev, 0, &tx_msg, sizeof(tx_msg));
    rt_kprintf("can0 loopback: write returned %d\n", (int)written);

    if (rt_device_control(dev, RT_CAN_CMD_GET_STATUS, &status) == RT_EOK)
    {
        rt_kprintf("can0 loopback: snd=%u drop=%u state=%u "
                   "txerr=%u rxerr=%u ackerr=%u\n",
                   (unsigned int)status.sndpkg,
                   (unsigned int)status.dropedsndpkg,
                   (unsigned int)status.errcode,
                   (unsigned int)status.snderrcnt,
                   (unsigned int)status.rcverrcnt,
                   (unsigned int)status.ackerrcnt);
    }

    rx_ret = rt_sem_take(&tc_can_rx_sem,
                         rt_tick_from_millisecond(TC_CAN_RX_WAIT));
    rt_kprintf("can0 loopback: rx wait %d\n", (int)rx_ret);

    uassert_int_equal(written, (rt_ssize_t)sizeof(tx_msg));
    uassert_int_equal(rx_ret, RT_EOK);

    if (rx_ret == RT_EOK)
    {
        rt_memset(&rx_msg, 0, sizeof(rx_msg));
        uassert_int_equal((int)rt_device_read(dev, 0, &rx_msg,
                                              sizeof(rx_msg)),
                          (int)sizeof(rx_msg));
        uassert_int_equal(rx_msg.id, TC_CAN_ID);
        uassert_int_equal(rx_msg.len, 3);
        uassert_int_equal(rx_msg.data[0], TC_CAN_DATA0);
        uassert_int_equal(rx_msg.data[1], TC_CAN_DATA1);
        uassert_int_equal(rx_msg.data[2], TC_CAN_DATA2);
    }

    /* Leave the controller in normal mode for the next user. */
    uassert_int_equal(rt_device_control(dev, RT_CAN_CMD_SET_MODE,
                                        (void *)(rt_ubase_t)RT_CAN_MODE_NORMAL),
                      RT_EOK);
    uassert_int_equal(rt_device_close(dev), RT_EOK);
    rt_sem_detach(&tc_can_rx_sem);
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_can_loopback);
}
UTEST_TC_EXPORT(testcase, "bsp.d213.can0.loopback", RT_NULL, RT_NULL, 10);

#endif /* BSP_USING_CAN0 && BSP_UTEST_CAN_LOOPBACK */
