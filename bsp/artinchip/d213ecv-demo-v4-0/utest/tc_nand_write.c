/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SPI NAND program/erase regression test.
 *
 * The test runs only on "nand_scratch", the MTD device the driver
 * registers over the physical blocks that no image_cfg.json partition
 * claims.  Erasing and programming there cannot touch the boot chain.
 *
 * The declared partitions are expected to stay read-only when
 * BSP_SPINAND_READONLY is set; that rejection is checked without
 * issuing any NAND command.
 *
 * This file is compiled only in builds that enable
 * BSP_SPINAND_SCRATCH_MTD; other builds get an empty translation unit
 * so the utest case count is unchanged.
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "utest.h"
#include "drivers/mtd_nand.h"

#ifdef BSP_SPINAND_SCRATCH_MTD

#define TC_NAND_PAGE_A      3U
#define TC_NAND_PAGE_B      4U

static struct rt_mtd_nand_device *require_scratch(void)
{
    struct rt_mtd_nand_device *dev;

    dev = (struct rt_mtd_nand_device *)rt_device_find("nand_scratch");
    if (dev == RT_NULL || dev->parent.type != RT_Device_Class_MTD)
    {
        rt_kprintf("tc_nand_write: nand_scratch missing -> FAIL\n");
        uassert_true(RT_FALSE);
        return RT_NULL;
    }
    if (dev->block_total == 0 || dev->pages_per_block == 0 ||
        dev->page_size == 0)
    {
        rt_kprintf("tc_nand_write: nand_scratch geometry invalid -> FAIL\n");
        uassert_true(RT_FALSE);
        return RT_NULL;
    }
    return dev;
}

static rt_bool_t require_ok(rt_err_t ret, const char *what)
{
    if (ret != RT_MTD_EOK && ret != RT_MTD_EECC_CORRECT)
    {
        rt_kprintf("tc_nand_write: %s failed (%d) -> FAIL\n", what, ret);
        uassert_true(RT_FALSE);
        return RT_FALSE;
    }
    return RT_TRUE;
}

static rt_bool_t buffer_is_erased(const rt_uint8_t *buf, rt_uint32_t len)
{
    rt_uint32_t i;

    for (i = 0; i < len; i++)
    {
        if (buf[i] != 0xFFU)
            return RT_FALSE;
    }
    return RT_TRUE;
}

/*
 * Find a factory-good scratch block.  Factory-bad blocks are skipped;
 * the test still fails if every block it is allowed to scan is bad.
 */
static rt_bool_t pick_good_block(struct rt_mtd_nand_device *dev,
                                 rt_uint32_t *block)
{
    rt_uint32_t b;

    for (b = 0; b < dev->block_total; b++)
    {
        rt_err_t ret = rt_mtd_nand_check_block(dev, b);

        if (ret == RT_MTD_EOK)
        {
            *block = b;
            return RT_TRUE;
        }
        if (ret != -RT_ERROR)
        {
            rt_kprintf("tc_nand_write: check_block(%u) failed (%d)\n", b, ret);
            return RT_FALSE;
        }
        rt_kprintf("tc_nand_write: scratch block %u is factory bad, skip\n", b);
    }
    return RT_FALSE;
}

/*
 * Erase the whole block and verify that every page reads back erased.
 * An ECC event on an erased page is reported separately: with on-chip
 * ECC it means the page is not cleanly erased or the ECC decoder
 * disagrees, so the test treats it as a failure.
 */
static rt_bool_t erase_and_verify(struct rt_mtd_nand_device *dev,
                                  rt_uint32_t block, rt_uint8_t *buf,
                                  rt_uint32_t page_len)
{
    rt_uint32_t p;
    rt_err_t ret;

    ret = rt_mtd_nand_erase_block(dev, block);
    if (!require_ok(ret, "erase_block"))
        return RT_FALSE;

    for (p = 0; p < dev->pages_per_block; p++)
    {
        rt_uint32_t page = block * dev->pages_per_block + p;

        ret = rt_mtd_nand_read(dev, page, buf, dev->page_size,
                               buf + dev->page_size, dev->oob_size);
        if (ret != RT_MTD_EOK)
        {
            rt_kprintf("tc_nand_write: erased page %u read returned %d\n",
                       p, ret);
            uassert_true(RT_FALSE);
            return RT_FALSE;
        }
        if (!buffer_is_erased(buf, page_len))
        {
            rt_kprintf("tc_nand_write: page %u not erased after erase\n", p);
            uassert_true(RT_FALSE);
            return RT_FALSE;
        }
    }
    return RT_TRUE;
}

static void tc_nand_scratch_write(void)
{
    struct rt_mtd_nand_device *dev;
    rt_uint8_t *pattern;
    rt_uint8_t *readback;
    rt_uint32_t page_len;
    rt_uint32_t block;
    rt_uint32_t page_a, page_b;
    rt_uint32_t i;
    rt_err_t ret;

    dev = require_scratch();
    if (dev == RT_NULL)
        return;

    if (!pick_good_block(dev, &block))
    {
        rt_kprintf("tc_nand_write: no good scratch block -> FAIL\n");
        uassert_true(RT_FALSE);
        return;
    }
    page_a = block * dev->pages_per_block + TC_NAND_PAGE_A;
    page_b = block * dev->pages_per_block + TC_NAND_PAGE_B;
    page_len = (rt_uint32_t)dev->page_size + dev->oob_size;

    pattern = (rt_uint8_t *)rt_malloc(page_len);
    readback = (rt_uint8_t *)rt_malloc(page_len);
    uassert_not_null(pattern);
    uassert_not_null(readback);
    if (pattern == RT_NULL || readback == RT_NULL)
    {
        rt_free(pattern);
        rt_free(readback);
        return;
    }

    rt_kprintf("tc_nand_write: nand_scratch %u blocks, testing block %u "
               "(physical %u..%u)\n", dev->block_total, block,
               dev->block_start, dev->block_start + dev->block_total - 1);

    /* 1. Start from a known erased block. */
    if (!erase_and_verify(dev, block, readback, page_len))
        goto out;

    /* 2. Program one page with a deterministic pattern. */
    for (i = 0; i < dev->page_size; i++)
        pattern[i] = (rt_uint8_t)((i * 13U + 0x5AU) & 0xFFU);
    rt_memset(pattern + dev->page_size, 0xFF, dev->oob_size);

    ret = rt_mtd_nand_write(dev, page_a, pattern, dev->page_size,
                            RT_NULL, 0);
    if (!require_ok(ret, "write_page"))
        goto out;

    /* 3. Read it back unchanged. */
    rt_memset(readback, 0, page_len);
    ret = rt_mtd_nand_read(dev, page_a, readback, dev->page_size,
                           readback + dev->page_size, dev->oob_size);
    if (!require_ok(ret, "read back"))
        goto out;
    if (rt_memcmp(readback, pattern, dev->page_size) != 0)
    {
        rt_kprintf("tc_nand_write: programmed data mismatch -> FAIL\n");
        uassert_true(RT_FALSE);
        goto out;
    }

    /* 4. The neighbour page must still be erased. */
    rt_memset(readback, 0, page_len);
    ret = rt_mtd_nand_read(dev, page_b, readback, dev->page_size,
                           readback + dev->page_size, dev->oob_size);
    if (!require_ok(ret, "neighbour read"))
        goto out;
    if (!buffer_is_erased(readback, page_len))
    {
        rt_kprintf("tc_nand_write: program disturbed the neighbour page\n");
        uassert_true(RT_FALSE);
        goto out;
    }

    /* 5. move_page copies data and OOB to an erased page. */
    ret = rt_mtd_nand_move_page(dev, page_a, page_b);
    if (!require_ok(ret, "move_page"))
        goto out;
    rt_memset(readback, 0, page_len);
    ret = rt_mtd_nand_read(dev, page_b, readback, dev->page_size,
                           readback + dev->page_size, dev->oob_size);
    if (!require_ok(ret, "moved page read"))
        goto out;
    if (rt_memcmp(readback, pattern, dev->page_size) != 0)
    {
        rt_kprintf("tc_nand_write: moved page mismatch -> FAIL\n");
        uassert_true(RT_FALSE);
        goto out;
    }

    /* 6. Erase again, leaving the scratch block clean for the next run. */
    if (!erase_and_verify(dev, block, readback, page_len))
        goto out;

    rt_kprintf("tc_nand_write: block %u erase/program/move/erase PASS "
               "(%u-byte page, %u-byte OOB)\n",
               block, dev->page_size, dev->oob_size);

out:
    rt_free(pattern);
    rt_free(readback);
}

/*
 * mark_badblock erases the block and writes OOB[0]=0x00; the following
 * erase restores OOB[0] to 0xFF, so the test leaves the scratch block
 * good again.  This also proves the bad-block marker path can program
 * the spare area through the same Program Load/Execute sequence.
 */
static void tc_nand_mark_bad(void)
{
    struct rt_mtd_nand_device *dev;
    rt_uint32_t block;

    dev = require_scratch();
    if (dev == RT_NULL)
        return;
    if (!pick_good_block(dev, &block))
    {
        rt_kprintf("tc_nand_write: no good scratch block for mark test\n");
        uassert_true(RT_FALSE);
        return;
    }

    uassert_int_equal(rt_mtd_nand_mark_badblock(dev, block), RT_MTD_EOK);
    uassert_int_equal(rt_mtd_nand_check_block(dev, block), -RT_ERROR);

    uassert_int_equal(rt_mtd_nand_erase_block(dev, block), RT_MTD_EOK);
    uassert_int_equal(rt_mtd_nand_check_block(dev, block), RT_MTD_EOK);

    rt_kprintf("tc_nand_write: block %u mark-bad/erase-marker PASS\n", block);
}

/*
 * With the default policy the declared partitions must reject every
 * mutating call before a NAND command is issued.  Guarded by the config
 * macro so a deliberately writable build never runs this check against
 * a real image partition.
 */
static void tc_nand_image_readonly(void)
{
#if defined(BSP_SPINAND_READONLY)
    struct rt_mtd_nand_device *dev;
    rt_uint8_t pattern[8] = {0};

    dev = (struct rt_mtd_nand_device *)rt_device_find("os");
    if (dev == RT_NULL || dev->parent.type != RT_Device_Class_MTD)
    {
        rt_kprintf("tc_nand_write: os partition missing -> FAIL\n");
        uassert_true(RT_FALSE);
        return;
    }

    uassert_int_equal(rt_mtd_nand_write(dev, 0, pattern, sizeof(pattern),
                                        RT_NULL, 0), -RT_EPERM);
    uassert_int_equal(rt_mtd_nand_erase_block(dev, 0), -RT_EPERM);
    uassert_int_equal(rt_mtd_nand_mark_badblock(dev, 0), -RT_EPERM);
    uassert_int_equal(rt_mtd_nand_move_page(dev, 0, 1), -RT_EPERM);
    rt_kprintf("tc_nand_write: os partition rejects program/erase/mark\n");
#endif
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_nand_image_readonly);
    UTEST_UNIT_RUN(tc_nand_scratch_write);
    UTEST_UNIT_RUN(tc_nand_mark_bad);
}
UTEST_TC_EXPORT(testcase, "bsp.d213.spinand.rw", RT_NULL, RT_NULL, 120);

#endif /* BSP_SPINAND_SCRATCH_MTD */
