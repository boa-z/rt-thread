/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Read-only SPI NAND regression tests.
 *
 * The tests never program, erase or mark a NAND block.  They verify
 * that those operations are rejected by the driver and that the read
 * path is repeatable, bounds checked and returns the real FIT image
 * header of the 'os' partition.
 *
 * Result semantics:
 *   - This file is only compiled when BSP_USING_SPINAND_MTD_NAND is
 *     enabled (SConscript + Kconfig gating); there is therefore no
 *     "quietly missing" case for the whole test case.
 *   - A missing or uninitialised NAND device inside an enabled build is
 *     a FAIL, never a SKIP: the driver registers itself during
 *     INIT_DEVICE_EXPORT, so absence means init misbehaved.
 *   - Each unit asserts with the utest framework; a test that cannot
 *     even reach its precondition fails immediately via uassert_true().
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "utest.h"
#include "drv_spinand.h"

#ifdef BSP_USING_SPINAND_MTD_NAND

#define TC_SPINAND_REPEAT_COUNT 100
#define TC_SPINAND_FIT_MAGIC    0xD00DFEEDU

/*
 * Partition layout from pack/image_cfg.json (spl blocks 0-7 ...
 * refresh 908-915, 916 blocks of 1024 assigned).  Hard-coded here on
 * purpose: the generated spinand_parts_gen.h is produced by the
 * drivers SConscript into the BSP build directory and is not part of
 * the utest build.  Keep this table in sync with pack/image_cfg.json
 * when the partition layout changes (the build-time host test
 * tools/test_nand_parts.py guards the generator source).
 */
static const struct tc_spinand_part
{
    const char *name;
    rt_uint32_t start_block;
    rt_uint32_t block_count;
    rt_bool_t   linear;
} tc_spinand_parts[] =
{
    {"spl",      0,   8, RT_TRUE},
    {"env",      8,   2, RT_TRUE},
    {"env_r",    10,  2, RT_TRUE},
    {"os",       12, 32, RT_TRUE},
    {"os_r",     44, 32, RT_TRUE},
    {"rodata",   76, 96, RT_TRUE},
    {"rodata_r", 172, 96, RT_TRUE},
    {"data",     268, 320, RT_FALSE},
    {"data_r",   588, 320, RT_FALSE},
    {"refresh",  908, 8, RT_TRUE},
};

/*
 * Precondition helpers: a missing precondition is a FAIL, not a SKIP.
 * They fail loudly (uassert_true RT_FALSE) and report RT_FALSE so the
 * caller can unwind cleanly.
 */
static rt_bool_t require_spinand_ready(void)
{
    if (aic_spinand_ro_ready() != RT_EOK)
    {
        rt_kprintf("tc_spinand: SPI NAND is NOT initialised in a build "
                   "that enables it -> FAIL\n");
        uassert_true(RT_FALSE);
        return RT_FALSE;
    }
    return RT_TRUE;
}

static struct rt_mtd_nand_device *require_mtd(const char *name)
{
    struct rt_mtd_nand_device *dev;

    dev = (struct rt_mtd_nand_device *)rt_device_find(name);
    if (dev == RT_NULL || dev->parent.type != RT_Device_Class_MTD)
    {
        rt_kprintf("tc_spinand: linear MTD device '%s' missing -> FAIL\n",
                   name);
        uassert_true(RT_FALSE);
        return RT_NULL;
    }
    return dev;
}

static rt_bool_t require_read_ok(rt_err_t ret)
{
    if (ret != RT_MTD_EOK && ret != RT_MTD_EECC_CORRECT)
    {
        rt_kprintf("tc_spinand: read failed (%d), expected EOK or "
                   "EECC_CORRECT -> FAIL\n", ret);
        uassert_true(RT_FALSE);
        return RT_FALSE;
    }
    return RT_TRUE;
}

static void tc_spinand_info(void)
{
    struct aic_spinand_ro_info info;

    if (!require_spinand_ready())
        return;
    uassert_int_equal(aic_spinand_ro_get_info(&info), RT_EOK);

    uassert_int_not_equal(info.id[0], 0x00);
    uassert_int_not_equal(info.id[0], 0xFF);
    uassert_not_null(info.model);
    uassert_true(info.page_size >= 512);
    uassert_true(info.oob_size > 0);
    uassert_true(info.pages_per_block > 0);
    uassert_true(info.block_count > 0);
    rt_kprintf("tc_spinand: %s id=%02X %02X %02X, %u+%u, %u x %u\n",
               info.model, info.id[0], info.id[1], info.id[2],
               info.page_size, info.oob_size,
               info.pages_per_block, info.block_count);
}

/*
 * The 8 linear MTD partitions must exist, match the partition table,
 * and the NFTL regions (data/data_r) must NOT appear as linear MTD.
 */
static void tc_spinand_partitions(void)
{
    struct aic_spinand_ro_info info;
    rt_uint32_t i;
    rt_err_t ret;

    if (!require_spinand_ready())
        return;
    if (aic_spinand_ro_get_info(&info) != RT_EOK)
        return;

    for (i = 0; i < sizeof(tc_spinand_parts) / sizeof(tc_spinand_parts[0]); i++)
    {
        const struct tc_spinand_part *part = &tc_spinand_parts[i];
        struct rt_mtd_nand_device *dev;
        rt_device_t raw;

        if (part->linear)
        {
            dev = require_mtd(part->name);
            if (dev == RT_NULL)
                continue;

            uassert_int_equal(dev->block_total, part->block_count);
            uassert_int_equal(dev->block_start, part->start_block);
            uassert_int_equal(dev->block_end,
                              part->start_block + part->block_count - 1);
            uassert_int_equal(dev->page_size, info.page_size);
            uassert_int_equal(dev->pages_per_block, info.pages_per_block);
        }
        else
        {
            /* NFTL-backed region: no linear MTD device, ever. */
            raw = rt_device_find(part->name);
            uassert_true(raw == RT_NULL || raw->type != RT_Device_Class_MTD);
        }
    }

    /* The os partition must be readable at its first page; if this read
     * fails the mapping between the partition start (block 12) and the
     * FIT image is broken. */
    {
        struct rt_mtd_nand_device *dev;
        rt_uint8_t *pagebuf;

        dev = require_mtd("os");
        if (dev != RT_NULL)
        {
            pagebuf = (rt_uint8_t *)rt_malloc(dev->page_size);
            uassert_not_null(pagebuf);
            if (pagebuf != RT_NULL)
            {
                ret = rt_mtd_nand_read(dev, 0, pagebuf, dev->page_size,
                                       RT_NULL, 0);
                (void)require_read_ok(ret);
                rt_free(pagebuf);
            }
        }
    }
}

static rt_uint32_t tc_be32(const rt_uint8_t *p)
{
    return ((rt_uint32_t)p[0] << 24) | ((rt_uint32_t)p[1] << 16) |
           ((rt_uint32_t)p[2] << 8) | (rt_uint32_t)p[3];
}

/*
 * The 'os' partition carries a FIT image (fallocated at pack time).
 * Page 0 must start with the FIT magic and a sane totalsize; this also
 * catches wrong page addressing (e.g. reading block 0 or a skipped
 * bad-block mapping) because the SPL/U-Boot header would look different.
 */
static void tc_spinand_fit_probe(void)
{
    struct rt_mtd_nand_device *dev;
    rt_uint8_t *pagebuf;
    rt_uint32_t magic, totalsize, part_size;
    rt_err_t ret;

    if (!require_spinand_ready())
        return;
    dev = require_mtd("os");
    if (dev == RT_NULL)
        return;

    part_size = (rt_uint32_t)dev->block_total * dev->pages_per_block *
                dev->page_size;
    pagebuf = (rt_uint8_t *)rt_malloc(dev->page_size);
    uassert_not_null(pagebuf);
    if (pagebuf == RT_NULL)
        return;

    ret = rt_mtd_nand_read(dev, 0, pagebuf, dev->page_size, RT_NULL, 0);
    if (!require_read_ok(ret))
    {
        rt_free(pagebuf);
        return;
    }

    magic = tc_be32(pagebuf);
    totalsize = tc_be32(pagebuf + 4);
    uassert_int_equal(magic, TC_SPINAND_FIT_MAGIC);
    uassert_true(totalsize >= 0x100U);
    uassert_true(totalsize <= part_size);
    rt_kprintf("tc_spinand: os FIT magic=0x%08X totalsize=%u "
               "(partition %u bytes)\n", magic, totalsize, part_size);

    rt_free(pagebuf);
}

/*
 * Repeat reads of os page 0 must return the same data every time.
 * Result statistics are printed so an ECC-corrected image is visible
 * even when the test passes.
 */
static void tc_spinand_repeat_read(void)
{
    struct rt_mtd_nand_device *dev;
    rt_uint8_t *first;
    rt_uint8_t *current;
    rt_uint32_t i;
    rt_uint32_t clean = 0, corrected = 0;
    rt_err_t ret;

    if (!require_spinand_ready())
        return;
    dev = require_mtd("os");
    if (dev == RT_NULL)
        return;

    first = (rt_uint8_t *)rt_malloc(dev->page_size);
    current = (rt_uint8_t *)rt_malloc(dev->page_size);
    uassert_not_null(first);
    uassert_not_null(current);
    if (first == RT_NULL || current == RT_NULL)
    {
        rt_free(first);
        rt_free(current);
        return;
    }

    ret = rt_mtd_nand_read(dev, 0, first, dev->page_size, RT_NULL, 0);
    if (!require_read_ok(ret))
    {
        rt_free(first);
        rt_free(current);
        return;
    }

    for (i = 0; i < TC_SPINAND_REPEAT_COUNT; i++)
    {
        ret = rt_mtd_nand_read(dev, 0, current, dev->page_size, RT_NULL, 0);
        if (ret != RT_MTD_EOK && ret != RT_MTD_EECC_CORRECT)
        {
            rt_kprintf("tc_spinand: repeat read %u failed (%d)\n", i, ret);
            uassert_true(RT_FALSE);
            break;
        }
        if (ret == RT_MTD_EOK)
            clean++;
        else
            corrected++;
        if (rt_memcmp(first, current, dev->page_size) != 0)
        {
            rt_kprintf("tc_spinand: page 0 data changed on read %u\n", i);
            uassert_true(RT_FALSE);
            break;
        }
    }
    uassert_int_equal(i, TC_SPINAND_REPEAT_COUNT);
    rt_kprintf("tc_spinand: %u repeat reads, %u clean, %u ECC-corrected\n",
               i, clean, corrected);

    rt_free(first);
    rt_free(current);
}

/*
 * Out-of-range page / block / length must be rejected with -RT_MTD_EIO
 * (negative, matching the in-tree driver convention) and never reported
 * as success or as a fabricated transfer error.
 */
static void tc_spinand_bounds(void)
{
    struct rt_mtd_nand_device *dev;
    rt_uint8_t *buf;
    rt_uint32_t out_page;
    rt_err_t ret;
    int result = SPINAND_RO_EIO;

    if (!require_spinand_ready())
        return;
    dev = require_mtd("os");
    if (dev == RT_NULL)
        return;

    buf = (rt_uint8_t *)rt_malloc(dev->page_size + 64);
    uassert_not_null(buf);
    if (buf == RT_NULL)
        return;

    out_page = (rt_uint32_t)dev->block_total * dev->pages_per_block;
    ret = rt_mtd_nand_read(dev, out_page, buf, dev->page_size, RT_NULL, 0);
    uassert_int_equal(ret, -RT_MTD_EIO);

    /* Oversized data length must be rejected before touching the lock. */
    ret = rt_mtd_nand_read(dev, 0, buf, dev->page_size + 1, RT_NULL, 0);
    uassert_int_equal(ret, -RT_MTD_EIO);

    uassert_int_not_equal(rt_mtd_nand_check_block(dev, dev->block_total),
                          RT_MTD_EOK);

    rt_free(buf);

    /* Raw helper must also reject out-of-range physical pages and the
     * "no buffer at all" request. */
    uassert_int_equal(aic_spinand_ro_read_page(0xFFFFFFFFU, RT_NULL, 0,
                                               RT_NULL, 0, &result),
                      SPINAND_RO_EINVAL);
    uassert_int_equal(aic_spinand_ro_read_page(0, RT_NULL, 0, RT_NULL, 0,
                                               &result),
                      SPINAND_RO_EINVAL);
}

/*
 * Read-only enforcement: no NAND contents may ever be modified, and the
 * generic rt_device entry points must not fake success either.
 */
static void tc_spinand_write_rejected(void)
{
    struct rt_mtd_nand_device *dev;
    rt_uint8_t pattern[8] = {0};

    if (!require_spinand_ready())
        return;
    dev = require_mtd("os");
    if (dev == RT_NULL)
        return;

    /* None of these calls may reach a NAND program/erase command. */
    uassert_int_equal(rt_mtd_nand_write(dev, 0, pattern, sizeof(pattern),
                                        RT_NULL, 0), -RT_EPERM);
    uassert_int_equal(rt_mtd_nand_erase_block(dev, 0), -RT_EPERM);
    uassert_int_equal(rt_mtd_nand_mark_badblock(dev, 0), -RT_EPERM);
    uassert_int_equal(rt_mtd_nand_move_page(dev, 0, 1), -RT_EPERM);

    /* The generic device entry points must not fake success either. */
    uassert_int_equal(rt_device_open(&dev->parent, RT_DEVICE_FLAG_RDONLY),
                      RT_EOK);
    uassert_int_equal(rt_device_read(&dev->parent, 0, pattern,
                                     sizeof(pattern)), -RT_ENOSYS);
    uassert_int_equal(rt_device_write(&dev->parent, 0, pattern,
                                      sizeof(pattern)), -RT_EPERM);
    uassert_int_equal(rt_device_close(&dev->parent), RT_EOK);
    uassert_int_equal(rt_device_control(&dev->parent, 0x7FFFU, RT_NULL),
                      -RT_ENOSYS);
}

/*
 * Physical bad-block markers must be readable (read-only path) and the
 * os start block must produce a verdict, never a transport error.
 */
static void tc_spinand_check_block(void)
{
    struct aic_spinand_ro_info info;
    struct rt_mtd_nand_device *dev;
    rt_bool_t bad = RT_FALSE;
    rt_err_t ret;

    if (!require_spinand_ready())
        return;
    uassert_int_equal(aic_spinand_ro_get_info(&info), RT_EOK);

    ret = aic_spinand_ro_check_block(0, &bad);
    uassert_int_equal(ret, RT_EOK);
    if (ret == RT_EOK)
        rt_kprintf("tc_spinand: physical block 0 is %s\n",
                   bad ? "factory bad" : "good");

    dev = require_mtd("os");
    if (dev == RT_NULL)
        return;

    /* Pass criteria are only RT_EOK (good) or -RT_ERROR (factory bad);
     * every other value is a transport/ECC failure, hence a FAIL. */
    ret = rt_mtd_nand_check_block(dev, 0);
    uassert_true(ret == RT_EOK || ret == -RT_ERROR);
    if (ret == RT_EOK || ret == -RT_ERROR)
        rt_kprintf("tc_spinand: os start block (abs %u) is %s\n",
                   dev->block_start,
                   (ret == RT_EOK) ? "good" : "factory bad");
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_spinand_info);
    UTEST_UNIT_RUN(tc_spinand_partitions);
    UTEST_UNIT_RUN(tc_spinand_fit_probe);
    UTEST_UNIT_RUN(tc_spinand_check_block);
    UTEST_UNIT_RUN(tc_spinand_repeat_read);
    UTEST_UNIT_RUN(tc_spinand_bounds);
    UTEST_UNIT_RUN(tc_spinand_write_rejected);
}
UTEST_TC_EXPORT(testcase, "bsp.d213.spinand.ro", RT_NULL, RT_NULL, 120);

#endif /* BSP_USING_SPINAND_MTD_NAND */
