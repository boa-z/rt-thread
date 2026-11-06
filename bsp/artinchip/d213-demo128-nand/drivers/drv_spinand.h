/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Public read-only SPI NAND helpers for the D213 BSP.
 *
 * The MTD NAND device is the primary interface.  These helpers exist for
 * diagnostics (MSH) and regression tests; they never program, erase or
 * mark blocks.
 */

#ifndef __DRV_SPINAND_H__
#define __DRV_SPINAND_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Result of a raw page read. */
#define SPINAND_RO_OK         0   /* data is valid, no ECC event */
#define SPINAND_RO_ECC_OK     1   /* data is valid, ECC corrected bit flips */
#define SPINAND_RO_EECC      -1   /* uncorrectable ECC error, data invalid */
#define SPINAND_RO_EIO       -2   /* transport / command failure */
#define SPINAND_RO_ETIMEOUT  -3   /* NAND busy timeout */
#define SPINAND_RO_EINVAL    -4   /* out of range or bad argument */

struct aic_spinand_ro_info
{
    rt_uint8_t  id[4];            /* raw JEDEC ID, most significant first */
    const char *model;
    rt_uint16_t page_size;
    rt_uint16_t oob_size;
    rt_uint16_t pages_per_block;
    rt_uint16_t block_count;
    rt_uint16_t ecc_strength;
    rt_uint32_t bus_hz;
};

/* Return RT_EOK when the flash was identified and initialised. */
rt_err_t aic_spinand_ro_ready(void);

rt_err_t aic_spinand_ro_get_info(struct aic_spinand_ro_info *info);

/* Read the NAND status register (0xC0) through GET_FEATURE (read-only). */
rt_err_t aic_spinand_ro_read_status(rt_uint8_t *status);

/* Read the NAND configuration register (0xB0) through GET_FEATURE. */
rt_err_t aic_spinand_ro_read_cfg(rt_uint8_t *cfg);

/*
 * Read a raw physical page.
 *   page      absolute page number, 0 .. block_count*pages_per_block-1
 *   data      optional main-area buffer
 *   data_len  must be <= page_size
 *   oob       optional spare-area buffer
 *   oob_len   must be <= oob_size
 *   result    SPINAND_RO_* value describing the ECC outcome
 * Returns SPINAND_RO_* directly (0/1 are success).
 */
int aic_spinand_ro_read_page(rt_uint32_t page,
                             rt_uint8_t *data, rt_uint32_t data_len,
                             rt_uint8_t *oob, rt_uint32_t oob_len,
                             int *result);

/*
 * Check a raw physical block before its first page is read.
 * bad = 1 for a factory-marked bad block, 0 for a good block.
 * Returns RT_EOK when the marker was read, a negative error otherwise.
 */
rt_err_t aic_spinand_ro_check_block(rt_uint32_t block, rt_bool_t *bad);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_SPINAND_H__ */
