/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 * The firmware image layout is board policy, not a NAND controller property.
 */
#include <rtthread.h>
#include <rtdevice.h>
#include "aic_board.h"
#include "spinand_parts_gen.h"

static struct rt_mtd_nand_device partition_devices[SPINAND_PART_COUNT];

const struct aic_spinand_layout aic_board_nand =
{
    .page_size = SPINAND_CFG_PAGE_SIZE, .oob_size = SPINAND_CFG_OOB_SIZE,
    .block_size = SPINAND_CFG_BLOCK_SIZE,
    .total_blocks = SPINAND_CFG_TOTAL_BLOCKS,
    .part_count = SPINAND_PART_COUNT, .parts = spinand_parts,
    .devices = partition_devices, .scratch_name = "nand_scratch",
};
