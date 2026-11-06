/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D12p CLIC platform hooks for the RT-Thread RISC-V common
 * interrupt controller.
 *
 * The common CLIC backend owns priority/IE/ATTR programming and the
 * direct-mode mcause dispatch.  D12p only needs to describe the CLIC
 * MMIO range and its vendor MINTTHRESH register.
 */

#include <rthw.h>
#include <rtthread.h>
#include <interrupt.h>

#include "board.h"

rt_ubase_t platform_get_clic_base(void)
{
    return AIC_CLIC_BASE;
}

rt_size_t platform_get_clic_size(void)
{
    return 0x2000UL;
}

rt_ubase_t platform_get_clic_quirks(void)
{
    /* D12p exposes the vendor MINTTHRESH register at CLIC + 0x0008. */
    return (1UL << 1);
}
