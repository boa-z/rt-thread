/*
 * Copyright (c) 2022-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal D21x syscfg provider for the USB0/USB1 PHYs.
 *
 * The register sequence is taken from the vendor
 * hal/syscfg/syscfg_hw_v1.0.h (Apache-2.0).  Only the fields that the
 * USB ports need are kept: the reference-resistor calibration and the
 * PHY0 host/device role bit.  LDO and GMAC setup stay with the
 * bootloader.
 */

#include "aic_compat.h"
#include "aic_hal_clk.h"
#include "hal_syscfg.h"

/* USB0_REXT / USB1_REXT / USB0_CFG, from syscfg_hw_v1.0.h. */
#define AIC_USB0_REXT           (SYSCFG_BASE + 0x048)
#define AIC_USB1_REXT           (SYSCFG_BASE + 0x04C)
#define AIC_USB0_CFG            (SYSCFG_BASE + 0x40C)
#define AIC_GMAC0_CFG           (SYSCFG_BASE + 0x410)

#define AIC_USB0_REXT_CAL_EN    BIT(8)
#define AIC_USB0_REXT_CAL_MASK  0xFFU
#define AIC_USB0_CFG_DRD_MODE   BIT(0)      /* 1 = device, 0 = host */

#define AIC_GMAC0_CFG_PHY_RGMII_SEL  BIT(0)
#define AIC_GMAC0_CFG_RMII_EXTCLK_SEL BIT(1)

/* Vendor default when the board does not override AIC_USB_RES_CAL. */
#define AIC_USB_RES_CAL         0x60U

void hal_syscfg_usb0_res_cal(u32 res_val)
{
    u32 val;

    val = readl(AIC_USB0_REXT);
    val &= ~AIC_USB0_REXT_CAL_MASK;
    val |= res_val & AIC_USB0_REXT_CAL_MASK;
    val |= AIC_USB0_REXT_CAL_EN;
    writel(val, AIC_USB0_REXT);
}

void hal_syscfg_usb1_res_cal(u32 res_val)
{
    u32 val;

    val = readl(AIC_USB1_REXT);
    val &= ~AIC_USB0_REXT_CAL_MASK;
    val |= res_val & AIC_USB0_REXT_CAL_MASK;
    val |= AIC_USB0_REXT_CAL_EN;
    writel(val, AIC_USB1_REXT);
}

void hal_syscfg_usb_phy0_sw_host(s32 host_mode)
{
    u32 val = readl(AIC_USB0_CFG);

    if (host_mode)
        val &= ~AIC_USB0_CFG_DRD_MODE;
    else
        val |= AIC_USB0_CFG_DRD_MODE;
    writel(val, AIC_USB0_CFG);
}

void hal_syscfg_usb_phy0_init(void)
{
    /* The bootloader normally leaves this gate open, but the vendor
     * probe enables and deasserts it explicitly. */
    hal_clk_enable_deassertrst(CLK_SYSCFG);
    hal_syscfg_usb0_res_cal(AIC_USB_RES_CAL);
}

void hal_syscfg_usb_phy1_init(void)
{
    /*
     * USB1 is host-only on D21x (there is no USB1_CFG role bit); only
     * the reference resistor needs calibrating.
     */
    hal_clk_enable_deassertrst(CLK_SYSCFG);
    hal_syscfg_usb1_res_cal(AIC_USB_RES_CAL);
}

/*
 * GMAC0 interface mode: the demo128 board uses RMII with the 50 MHz
 * REF_CLK driven by the PHY, so clear the RGMII select and set the
 * external-clock bit.
 */
void hal_syscfg_gmac0_init(void)
{
    u32 val;

    hal_clk_enable_deassertrst(CLK_SYSCFG);

    val = readl(AIC_GMAC0_CFG);
    val &= ~AIC_GMAC0_CFG_PHY_RGMII_SEL;
    val |= AIC_GMAC0_CFG_RMII_EXTCLK_SEL;
    writel(val, AIC_GMAC0_CFG);
}
