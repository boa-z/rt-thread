/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 * Board-provided configuration consumed by the shared device adapters.
 */
#ifndef __AIC_BOARD_H__
#define __AIC_BOARD_H__

#include <stdint.h>
#include <string.h>

struct aic_panel_config
{
    uint32_t width, height, pixclock;
    uint32_t hfp, hbp, hsw, vfp, vbp, vsw;
    uint32_t lvds_link, lvds_mode, lanes, polarity, phy;
    uint32_t hsync_positive, vsync_positive, fb_count;
    uint32_t ui_bg_blend;
    int (*set_power)(int on);
};

struct aic_touch_config
{
    const char *i2c_bus, *reset_pin, *interrupt_pin;
    uint32_t x_range, y_range;
};

struct aic_board_audio_config
{
    const char *pa_pin;
    uint32_t pa_active_low, pa_settle_ms, speaker;
};

struct aic_qspi_config
{
    uint32_t input_hz, max_hz;
    /* RX sampling delay in half SCK cycles: 0, 1 or 2. */
    uint32_t rx_delay_half_cycles;
};

struct aic_sdmc_slot
{
    const char *block_name;
    uint32_t bus_width, max_hz, is_sdio;
    /* D21x phase selector: 0/90/180/270 degrees. */
    uint32_t drive_phase, sample_phase;
    int (*power_on)(void);
};

struct spinand_part_desc
{
    const char *name;
    uint32_t start_block, block_count;
    uint8_t linear;
};

struct rt_mtd_nand_device;
struct aic_spinand_layout
{
    uint32_t page_size, oob_size, block_size, total_blocks, part_count;
    const struct spinand_part_desc *parts;
    /* One persistent device object per partition, owned by the board. */
    struct rt_mtd_nand_device *devices;
    const char *scratch_name;
};

struct aic_storage_config
{
    const char *rodata_partition, *rodata_device, *rodata_mount;
    const char *lfs_partition, *lfs_mount, *lfs_stress_path;
};

/* Validate board metadata before any partition object is registered. Use
 * subtraction for bounds so a malformed descriptor cannot wrap an end block. */
static inline int aic_spinand_layout_check(const struct aic_spinand_layout *l)
{
    uint32_t i, j, end = 0;

    if (!l || !l->parts || !l->devices || !l->page_size || !l->block_size ||
        l->block_size % l->page_size || !l->total_blocks || !l->part_count ||
        l->part_count > l->total_blocks || !l->scratch_name || !l->scratch_name[0])
        return -1;
    for (i = 0; i < l->part_count; i++)
    {
        const struct spinand_part_desc *p = &l->parts[i];
        if (!p->name || !p->name[0] || p->linear > 1 || !p->block_count ||
            p->start_block < end || p->start_block >= l->total_blocks ||
            p->block_count > l->total_blocks - p->start_block ||
            strcmp(p->name, l->scratch_name) == 0)
            return -1;
        for (j = 0; j < i; j++)
            if (strcmp(p->name, l->parts[j].name) == 0)
                return -1;
        end = p->start_block + p->block_count;
    }
    return 0;
}

/* Define only the descriptors required by the enabled devices. Their lifetime
 * must cover all device accesses; drivers never modify these descriptors. */
extern const struct aic_panel_config aic_board_panel;
extern const struct aic_touch_config aic_board_touch;
extern const struct aic_board_audio_config aic_board_audio;
extern const struct aic_qspi_config aic_board_qspi0;
extern const struct aic_spinand_layout aic_board_nand;
extern const struct aic_storage_config aic_board_storage;
const struct aic_sdmc_slot *aic_board_sdmc_slot(unsigned int index);
int aic_board_mac_prepare(unsigned int port);

#endif /* __AIC_BOARD_H__ */
