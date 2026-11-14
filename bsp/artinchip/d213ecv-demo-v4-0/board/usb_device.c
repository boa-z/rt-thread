/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * D213 USB0 device-mode glue for the CherryUSB AIC UDC port.
 *
 * The controller programming lives in
 * components/drivers/usb/cherryusb/port/aic (Apache-2.0).  This file
 * registers one CDC ACM function and exposes it as the RT-Thread
 * character device "usb-acm0" through
 * platform/rtthread/rt_usbd_serial.c, so the port can be used as a
 * secondary console or a data channel without a UART cable.
 *
 * The descriptor layout follows the CherryUSB CDC ACM chardev template
 * (Copyright (c) 2025, sakumisu, Apache-2.0).  The PHY role switch,
 * clock/reset sequencing and IRQ registration are performed by
 * usb_dc_low_level_init() in the AIC port; board/pinmux.c routes
 * PO.0/PO.1 for USB0.
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "aic_compat.h"
#include "usbd_core.h"
#include "usbd_cdc_acm.h"

#if defined(BSP_USING_USB0_DEVICE)

#ifdef RT_USING_FINSH
#include <finsh.h>
#endif

/* Endpoint addresses (HS: bulk max packet size is 512). */
#define USBDEV_CDC_IN_EP   0x81
#define USBDEV_CDC_OUT_EP  0x02
#define USBDEV_CDC_INT_EP  0x83

/* ArtInChip vendor ID; PID is local to this BSP. */
#define USBDEV_VID         0x33C3
#define USBDEV_PID         0x6688
#define USBDEV_MAX_POWER   100

#define USBDEV_CONFIG_SIZE (9 + CDC_ACM_DESCRIPTOR_LEN)

#ifdef CONFIG_USB_HS
#define USBDEV_CDC_MPS     512
#else
#define USBDEV_CDC_MPS     64
#endif

static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, USBDEV_VID,
                               USBDEV_PID, 0x0100, 0x01)
};

static const uint8_t config_descriptor[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USBDEV_CONFIG_SIZE, 0x02, 0x01,
                               USB_CONFIG_BUS_POWERED, USBDEV_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, USBDEV_CDC_INT_EP, USBDEV_CDC_OUT_EP,
                            USBDEV_CDC_IN_EP, USBDEV_CDC_MPS, 0x02)
};

static const uint8_t device_quality_descriptor[] = {
    0x0a,
    USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
    0x00, 0x02,
    0x00, 0x00,
    0x00, 0x40,
    0x00, 0x00,
};

static const char *string_descriptors[] = {
    (const char[]){ 0x09, 0x04 },       /* Langid */
    "ArtInChip",                        /* Manufacturer */
    "D213 CDC ACM",                     /* Product */
    "d213-demo128",                     /* Serial Number */
};

static const uint8_t *usbdev_device_descriptor(uint8_t speed)
{
    return device_descriptor;
}

static const uint8_t *usbdev_config_descriptor(uint8_t speed)
{
    return config_descriptor;
}

static const uint8_t *usbdev_quality_descriptor(uint8_t speed)
{
    return device_quality_descriptor;
}

static const char *usbdev_string_descriptor(uint8_t speed, uint8_t index)
{
    if (index >= (sizeof(string_descriptors) / sizeof(char *)))
        return NULL;
    return string_descriptors[index];
}

static const struct usb_descriptor usbdev_descriptor = {
    .device_descriptor_callback = usbdev_device_descriptor,
    .config_descriptor_callback = usbdev_config_descriptor,
    .device_quality_descriptor_callback = usbdev_quality_descriptor,
    .string_descriptor_callback = usbdev_string_descriptor
};

static void usbdev_event_handler(uint8_t busid, uint8_t event)
{
    /* No work is required; enumeration is driven by usbd_core. */
    (void)busid;
    (void)event;
}

void usbd_cdc_acm_serial_init(uint8_t busid, uint8_t in_ep, uint8_t out_ep);

static int rt_hw_usb_dev_init(void)
{
    usbd_desc_register(0, &usbdev_descriptor);
    usbd_cdc_acm_serial_init(0, USBDEV_CDC_IN_EP, USBDEV_CDC_OUT_EP);
    usbd_initialize(0, USB_DEV_BASE, usbdev_event_handler);

    return 0;
}
INIT_APP_EXPORT(rt_hw_usb_dev_init);

static int usb_dev_status(int argc, char **argv)
{
    rt_device_t dev = rt_device_find("usb-acm0");
    rt_device_t console = rt_console_get_device();

    rt_kprintf("usb-dev: usb-acm0 %s, configured=%d, console=%s\n",
               dev ? "registered" : "missing",
               usb_device_is_configured(0),
               console ? console->parent.name : "none");
    return 0;
}
MSH_CMD_EXPORT(usb_dev_status, dump usb device status);

#ifdef RT_USING_FINSH
static int usb_dev_console(int argc, char **argv)
{
    if (argc != 2)
    {
        rt_kprintf("usage: usb_dev_console on|off\n");
        return -RT_EINVAL;
    }

    if (!rt_strcmp(argv[1], "on"))
    {
        if (rt_device_find("usb-acm0") == RT_NULL)
        {
            rt_kprintf("usb-dev: usb-acm0 is not registered\n");
            return -RT_ENOSYS;
        }
        if (!usb_device_is_configured(0))
        {
            /* usbd_serial_open() waits for enumeration; refuse instead
             * of blocking the console thread forever. */
            rt_kprintf("usb-dev: USB0 is not configured; connect it to a host first\n");
            return -RT_EPERM;
        }
        finsh_set_device("usb-acm0");
        rt_console_set_device("usb-acm0");
    }
    else if (!rt_strcmp(argv[1], "off"))
    {
        /*
         * rt_console_set_device() opens the device with RDWR|STREAM, i.e.
         * without RT_DEVICE_FLAG_INT_RX.  serial-v1's rt_serial_open()
         * rewrites dev->open_flag on every open and only re-adds INT_RX
         * when that open asks for it, so the console open must come
         * first.  If the finsh open (which asks for INT_RX) came last,
         * uart0 would keep its RX FIFO while open_flag lost INT_RX: the
         * shell would poll the hardware FIFO directly, the driver's 1 ms
         * RX poller would drain the same bytes into the software ring,
         * and the serial console would stop accepting input until reset.
         */
        rt_console_set_device(RT_CONSOLE_DEVICE_NAME);
        finsh_set_device(RT_CONSOLE_DEVICE_NAME);
    }
    else
    {
        rt_kprintf("usage: usb_dev_console on|off\n");
        return -RT_EINVAL;
    }

    return 0;
}
MSH_CMD_EXPORT(usb_dev_console, switch console to usb-acm0);
#endif

#endif /* BSP_USING_USB0_DEVICE */
