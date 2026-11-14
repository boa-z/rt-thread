/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * D213 USB host glue for the standard CherryUSB host stack.
 *
 * The controller programming lives in
 * components/drivers/usb/cherryusb/port/ehci (AIC glue, Apache-2.0) and
 * is selected by RT_CHERRYUSB_HOST_EHCI_AIC.  This file only provides
 * the two pieces that the generic component cannot know about:
 *
 *   - the D-Cache maintenance hooks, because the C906 D-Cache is not
 *     coherent with the EHCI/OHCI DMA engines;
 *   - the PHY reference-resistor calibration before the controller is
 *     initialised (the role switch itself is done by the AIC glue
 *     through hal_syscfg_usb_phy0_sw_host()).
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <stdint.h>

#include "aic_core.h"
#include "aic_cache.h"
#include "hal_syscfg.h"

#if defined(BSP_USING_USB0_HOST) || defined(BSP_USING_USB1_HOST)

/*
 * CherryUSB calls these when CONFIG_USB_DCACHE_ENABLE is set (see
 * board/usb_config.h).  Descriptors are written by the CPU and read by
 * the controller, so a clean is enough before the hardware reads them;
 * before the CPU reads data the controller wrote, a clean+invalidate is
 * used.  Pure invalidate is avoided on purpose: line granularity means
 * an adjacent dirty variable could otherwise be discarded.
 */
void usb_dcache_clean(uintptr_t addr, size_t size)
{
    aic_dcache_clean_range(addr, size);
}

void usb_dcache_invalidate(uintptr_t addr, size_t size)
{
    aic_dcache_clean_invalidate_range(addr, size);
}

void usb_dcache_flush(uintptr_t addr, size_t size)
{
    aic_dcache_clean_invalidate_range(addr, size);
}

static int rt_hw_usb_init(void)
{
#ifdef BSP_USING_USB0_HOST
    hal_syscfg_usb_phy0_init();
    pr_info("usb: USB0 host PHY ready (EHCI + OHCI companion)\n");
#endif
#ifdef BSP_USING_USB1_HOST
    hal_syscfg_usb_phy1_init();
    pr_info("usb: USB1 host PHY ready (EHCI + OHCI companion)\n");
#endif
    return 0;
}
INIT_BOARD_EXPORT(rt_hw_usb_init);

/*
 * Board bring-up helper: dump the EHCI/OHCI root-port state.  The
 * controller does not latch connect events into the shell, so this is
 * the quickest way to tell a wiring/VBUS problem from a software one.
 */
#ifdef BSP_USING_USB1_HOST
#define USB_HC_BASE   USB_HOST1_BASE
#else
#define USB_HC_BASE   USB_HOST0_BASE
#endif

#define USB_EHCI_PORTSC_CCS    (1U << 0)
#define USB_EHCI_PORTSC_CSC    (1U << 1)
#define USB_EHCI_PORTSC_PE     (1U << 2)
#define USB_EHCI_PORTSC_OCA    (1U << 4)
#define USB_EHCI_PORTSC_PP     (1U << 12)
#define USB_EHCI_PORTSC_OWNER  (1U << 13)

static int usb_status(int argc, char **argv)
{
    uint32_t caplength = readl((volatile void *)USB_HC_BASE) & 0xffU;
    uint32_t usbsts = readl((volatile void *)(USB_HC_BASE + caplength + 0x04));
    uint32_t usbintr = readl((volatile void *)(USB_HC_BASE + caplength + 0x08));
    uint32_t portsc = readl((volatile void *)(USB_HC_BASE + caplength + 0x44));
    uint32_t phy = readl((volatile void *)(USB_HC_BASE + 0x800));
    uint32_t ohci = readl((volatile void *)(USB_HC_BASE + 0x400 + 0x54));

    rt_kprintf("usb: EHCI caplength=0x%02x USBSTS=0x%08x USBINTR=0x%08x\n",
               caplength, usbsts, usbintr);
    rt_kprintf("usb: PHY_CFG=0x%08x PORTSC=0x%08x "
               "(ccs=%d csc=%d pe=%d oca=%d pp=%d owner=%d)\n",
               phy, portsc,
               !!(portsc & USB_EHCI_PORTSC_CCS),
               !!(portsc & USB_EHCI_PORTSC_CSC),
               !!(portsc & USB_EHCI_PORTSC_PE),
               !!(portsc & USB_EHCI_PORTSC_OCA),
               !!(portsc & USB_EHCI_PORTSC_PP),
               !!(portsc & USB_EHCI_PORTSC_OWNER));
    rt_kprintf("usb: OHCI HcRhPortStatus1=0x%08x (ccs=%d pes=%d pps=%d lsda=%d)\n",
               ohci, !!(ohci & 1), !!(ohci & (1U << 2)),
               !!(ohci & (1U << 8)), !!(ohci & (1U << 9)));
    return 0;
}
MSH_CMD_EXPORT(usb_status, dump usb root port status);

#endif /* BSP_USING_USB0_HOST || BSP_USING_USB1_HOST */
