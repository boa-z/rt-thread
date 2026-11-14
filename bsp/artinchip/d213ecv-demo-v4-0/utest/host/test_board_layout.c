/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdint.h>
#include "aic_board.h"

/* Only object identity is needed by the descriptor validator. */
struct rt_mtd_nand_device { int unused; };

int main(void)
{
    struct rt_mtd_nand_device devices[2];
    struct spinand_part_desc parts[] = {
        {"boot", 0, 8, 1}, {"data", 16, 16, 0},
    };
    struct aic_spinand_layout valid = {
        2048, 64, 131072, 1024, 2, parts, devices, "scratch"
    };
    struct aic_spinand_layout layout;

    assert(aic_spinand_layout_check(&valid) == 0);
    assert(aic_spinand_layout_check(NULL) != 0);
    layout = valid; layout.parts = NULL;
    assert(aic_spinand_layout_check(&layout) != 0);
    layout = valid; layout.devices = NULL;
    assert(aic_spinand_layout_check(&layout) != 0);
    layout = valid; layout.page_size = 0;
    assert(aic_spinand_layout_check(&layout) != 0);
    layout = valid; layout.block_size++;
    assert(aic_spinand_layout_check(&layout) != 0);
    layout = valid; layout.part_count = 1025;
    assert(aic_spinand_layout_check(&layout) != 0);
    parts[1].start_block = 7; /* overlap */
    assert(aic_spinand_layout_check(&valid) != 0);
    parts[1].start_block = UINT32_MAX - 3;
    assert(aic_spinand_layout_check(&valid) != 0);
    parts[1].start_block = 16; parts[1].block_count = UINT32_MAX;
    assert(aic_spinand_layout_check(&valid) != 0);
    parts[1].block_count = 16; parts[1].name = "boot";
    assert(aic_spinand_layout_check(&valid) != 0);
    parts[1].name = "scratch";
    assert(aic_spinand_layout_check(&valid) != 0);
    parts[1].name = NULL;
    assert(aic_spinand_layout_check(&valid) != 0);
    parts[1].name = "data"; parts[1].linear = 2;
    assert(aic_spinand_layout_check(&valid) != 0);
    parts[1].linear = 0;
    /* A different board may use larger pages and a partition ending exactly
     * at the last block, including gaps before its first partition. */
    layout = valid; layout.page_size = 4096; layout.total_blocks = 32;
    assert(aic_spinand_layout_check(&layout) == 0);
    layout.total_blocks = 31;
    assert(aic_spinand_layout_check(&layout) != 0);
    return 0;
}
