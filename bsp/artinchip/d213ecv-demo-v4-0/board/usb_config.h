/*
 * Copyright (c) 2022, sakumisu
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CherryUSB configuration for the ArtInChip D213 USB host ports.
 *
 * Only the host stack (EHCI + OHCI companion) and the MSC class are
 * enabled by this BSP.  The port is high-speed capable; low/full-speed
 * devices are handled through the companion OHCI controller at
 * USB_HOSTx_BASE + 0x400.
 *
 * USB_NOCACHE_RAM_SECTION has no uncached RAM window on this SoC: the
 * C906 D-Cache is maintained explicitly through the usb_dcache_* hooks
 * implemented in ../libraries/soc/d21x/drivers/drv_usb.c, so the descriptors only need cache
 * line alignment.
 */

#ifndef CHERRYUSB_CONFIG_H
#define CHERRYUSB_CONFIG_H

/* ================ USB common Configuration ================ */

#include <rtthread.h>

#define CONFIG_USB_PRINTF(...) rt_kprintf(__VA_ARGS__)

#ifndef CONFIG_USB_DBG_LEVEL
#define CONFIG_USB_DBG_LEVEL USB_DBG_INFO
#endif

#define CONFIG_USB_DCACHE_ENABLE

/* 64-byte cache line on the C906. */
#define CONFIG_USB_ALIGN_SIZE 64

#define USB_NOCACHE_RAM_SECTION \
    __attribute__((aligned(CONFIG_USB_ALIGN_SIZE)))

/* ================ USB Device Stack Configuration ================ */

#if defined(BSP_USING_USB0_DEVICE)

#include "aic_clk_id.h"
#include "aic_reset_id.h"

/*
 * D213 USB0 device controller (AIC UDC).  The port selects the
 * high-speed PHY; the base address, clock/reset IDs and IRQ number come
 * from the D21x SoC definitions used by the vendor port.
 */
#define CONFIG_USB_AIC_DC_PORT       1            /* HS_PORT */
#define CONFIG_USB_AIC_DC_BASE       0x10200000UL /* USB_DEV_BASE */
#define CONFIG_USB_AIC_DC_CLK        CLK_USBD
#define CONFIG_USB_AIC_DC_RESET      RESET_USBD
#define CONFIG_USB_AIC_DC_PHY_CLK    CLK_USB_PHY0
#define CONFIG_USB_AIC_DC_PHY_RESET  RESET_USBPHY0
#define CONFIG_USB_AIC_DC_IRQ_NUM    34           /* USB_DEV_IRQn */

#endif

#ifndef CONFIG_USBDEV_REQUEST_BUFFER_LEN
#define CONFIG_USBDEV_REQUEST_BUFFER_LEN 512
#endif

#ifndef CONFIG_USBDEV_MAX_BUS
#define CONFIG_USBDEV_MAX_BUS 1
#endif

#ifndef CONFIG_USBDEV_EP_NUM
#define CONFIG_USBDEV_EP_NUM 5
#endif

/* ================ USB HOST Stack Configuration ================== */

#define CONFIG_USBHOST_MAX_RHPORTS          1
#define CONFIG_USBHOST_MAX_EXTHUBS          1
#define CONFIG_USBHOST_MAX_EHPORTS          4
#define CONFIG_USBHOST_MAX_INTERFACES       8
#define CONFIG_USBHOST_MAX_INTF_ALTSETTINGS 2
#define CONFIG_USBHOST_MAX_ENDPOINTS        4

#define CONFIG_USBHOST_MAX_MSC_CLASS        1

#define CONFIG_USBHOST_DEV_NAMELEN          16

#ifndef CONFIG_USBHOST_PSC_PRIO
#define CONFIG_USBHOST_PSC_PRIO             0
#endif
#ifndef CONFIG_USBHOST_PSC_STACKSIZE
#define CONFIG_USBHOST_PSC_STACKSIZE        4096
#endif

/* Control transfer buffer (must hold one 512-byte setup data stage). */
#ifndef CONFIG_USBHOST_REQUEST_BUFFER_LEN
#define CONFIG_USBHOST_REQUEST_BUFFER_LEN   512
#endif

#ifndef CONFIG_USBHOST_CONTROL_TRANSFER_TIMEOUT
#define CONFIG_USBHOST_CONTROL_TRANSFER_TIMEOUT 500
#endif

#ifndef CONFIG_USBHOST_MSC_TIMEOUT
#define CONFIG_USBHOST_MSC_TIMEOUT          5000
#endif

/* Mount point used by platform/rtthread/rt_usbh_msc.c.  "/" is legal
 * without a separate root filesystem. */
#ifndef CONFIG_USB_DFS_MOUNT_POINT
#define CONFIG_USB_DFS_MOUNT_POINT          "/"
#endif

/* ================ USB Host Port Configuration ================== */

#ifndef CONFIG_USBHOST_MAX_BUS
#define CONFIG_USBHOST_MAX_BUS              1
#endif

#ifndef CONFIG_USBHOST_PIPE_NUM
#define CONFIG_USBHOST_PIPE_NUM             10
#endif

/* ---------------- EHCI Configuration ---------------- */

#define CONFIG_USB_EHCI_HCCR_OFFSET         0x0
#define CONFIG_USB_EHCI_FRAME_LIST_SIZE     1024
#define CONFIG_USB_EHCI_QH_NUM              CONFIG_USBHOST_PIPE_NUM
#define CONFIG_USB_EHCI_QTD_NUM             (CONFIG_USB_EHCI_QH_NUM * 3)
#define CONFIG_USB_EHCI_ITD_NUM             4
/* The AIC glue and the vendor controller require these. */
#define CONFIG_USB_EHCI_CONFIGFLAG
#define CONFIG_USB_EHCI_WITH_OHCI
#define CONFIG_USB_EHCI_DESC_DCACHE_ENABLE

/* ---------------- OHCI Configuration (companion) ---------------- */

#define CONFIG_USB_OHCI_HCOR_OFFSET         0x400
#define CONFIG_USB_OHCI_ED_NUM              CONFIG_USBHOST_PIPE_NUM
#define CONFIG_USB_OHCI_TD_NUM              3
#define CONFIG_USB_OHCI_DESC_DCACHE_ENABLE

/* No address translation on this SoC. */
#ifndef usb_phyaddr2ramaddr
#define usb_phyaddr2ramaddr(addr) (addr)
#endif

#ifndef usb_ramaddr2phyaddr
#define usb_ramaddr2phyaddr(addr) (addr)
#endif

#endif /* CHERRYUSB_CONFIG_H */
