/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal D21x reset provider for the display and GE HAL.
 *
 * Bit 13 of each CMU peripheral register is the active-low reset release
 * bit (1 = deasserted, 0 = asserted).  The full vendor reset table is not
 * needed by this BSP; only the display/GE signals are implemented.
 */

#include <stdint.h>

#include "aic_compat.h"
#include "aic_clk_id.h"
#include "aic_hal_reset.h"

struct aic_reset_desc
{
    u32 offset;
};

static int aic_reset_lookup(u32 id, struct aic_reset_desc *desc)
{
    switch (id)
    {
    case RESET_RGB:
        desc->offset = CLK_RGB_REG;
        return 0;
    case RESET_LVDS:
        desc->offset = CLK_LVDS_REG;
        return 0;
    case RESET_MIPIDSI:
        desc->offset = CLK_MIPID_REG;
        return 0;
    case RESET_DE:
        desc->offset = CLK_DE_REG;
        return 0;
    case RESET_GE:
        desc->offset = CLK_GE_REG;
        return 0;
    default:
        return -EINVAL;
    }
}

static int aic_reset_set(u32 id, int deassert)
{
    struct aic_reset_desc desc;
    u32 value;

    if (aic_reset_lookup(id, &desc) < 0)
        return -EINVAL;

    value = readl(CMU_BASE + desc.offset);
    if (deassert)
        value |= BIT(13);
    else
        value &= ~BIT(13);
    writel(value, CMU_BASE + desc.offset);

    return 0;
}

int hal_reset_assert(uint32_t rst_id)
{
    return aic_reset_set(rst_id, 0);
}

int hal_reset_deassert(uint32_t rst_id)
{
    int ret = aic_reset_set(rst_id, 1);
    if (ret == 0)
        udelay(30);
    return ret;
}

int hal_reset_status(uint32_t rst_id)
{
    struct aic_reset_desc desc;

    if (aic_reset_lookup(rst_id, &desc) < 0)
        return -EINVAL;

    return !!(readl(CMU_BASE + desc.offset) & BIT(13));
}
