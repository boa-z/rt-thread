/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * LittleFS over the board-selected writable MTD partition. The board
 * supplies the partition name and mount/stress paths via aic_board_storage.
 * Image partitions remain read-only under the default board policy.
 *
 * Filesystem blocks are mapped through a persistent bad-block table:
 *
 *   - physical blocks are scanned for the factory marker at mount time;
 *   - two of the first good blocks hold alternating BBT copies;
 *   - the remaining good blocks are split into LittleFS blocks and
 *     spare blocks;
 *   - a block that fails erase/program at run time is replaced from the
 *     spare pool and the mapping is persisted before returning.
 *
 * The logical block size is the physical erase block, so LittleFS sees a
 * linear device of 'logical' blocks.  The BBT is the only metadata this
 * layer adds; LittleFS itself stays the unmodified upstream v2.5
 * implementation.
 */

#include <rtthread.h>
#include "aic_board.h"
#include <rtdevice.h>
#include <dfs_fs.h>
#include <dfs_file.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>

#include "lfs.h"
#include "drv_nand_lfs.h"

#define LFS_NAND_MTD_NAME       aic_board_storage.lfs_partition
#define LFS_FS_NAME             "lfs"
#define LFS_NAND_MOUNT_POINT    aic_board_storage.lfs_mount

#define BBT_VERSION             1U
#define BBT_HEADER_SIZE         24U
#define BBT_OFF_MAGIC           0U
#define BBT_OFF_VERSION         4U
#define BBT_OFF_HEADER_SIZE     6U
#define BBT_OFF_SEQUENCE        8U
#define BBT_OFF_TOTAL           12U
#define BBT_OFF_LOGICAL         14U
#define BBT_OFF_SLOT0           16U
#define BBT_OFF_SLOT1           18U
#define BBT_OFF_SPARE           20U
#define BBT_OFF_RESERVED        22U

#define BBT_STATE_MAPPED        0U
#define BBT_STATE_BAD           1U
#define BBT_STATE_SLOT          2U
#define BBT_STATE_SPARE         3U

#define BBT_NO_BLOCK            0xFFFFU
#define BBT_SEQ_WRAP_LIMIT      0x7FFFFFFFU

#ifndef BSP_SPINAND_LITTLEFS_SPARE_BLOCKS
#define BSP_SPINAND_LITTLEFS_SPARE_BLOCKS 4
#endif

#define LFS_BLOCK_CYCLES        512
#define LFS_LOOKAHEAD_SIZE      32
#define LFS_MIN_LOGICAL_BLOCKS  4

struct nand_lfs
{
    struct rt_mutex lock;
    struct rt_mtd_nand_device *mtd;

    rt_uint8_t *state;          /* total entries: BBT_STATE_* */
    rt_uint16_t *map;           /* logical -> physical */
    rt_uint8_t *scan;           /* factory bad scan, 1 = bad */
    rt_uint8_t *record;         /* BBT record buffer */
    rt_uint8_t *readbuf;        /* second BBT slot read buffer */

    rt_uint16_t total;          /* physical blocks in the partition */
    rt_uint16_t logical;        /* LittleFS blocks */
    rt_uint16_t slots[2];       /* physical blocks holding the BBT */
    rt_uint16_t spare_count;
    rt_uint16_t bad_count;
    rt_uint32_t sequence;

    rt_int8_t active_slot;      /* 0/1, -1 when no valid copy */
    rt_bool_t created;          /* BBT built during this boot */
    rt_bool_t ready;
};

static struct nand_lfs nand_lfs;
static struct lfs lfs_fs;
static struct lfs_config lfs_cfg;
static rt_bool_t lfs_is_mounted;
static rt_bool_t bbt_reboot_required;

static rt_uint16_t get_u16(const rt_uint8_t *p)
{
    return (rt_uint16_t)(p[0] | ((rt_uint16_t)p[1] << 8));
}

static rt_uint32_t get_u32(const rt_uint8_t *p)
{
    return (rt_uint32_t)p[0] |
           ((rt_uint32_t)p[1] << 8) |
           ((rt_uint32_t)p[2] << 16) |
           ((rt_uint32_t)p[3] << 24);
}

static void put_u16(rt_uint8_t *p, rt_uint16_t v)
{
    p[0] = (rt_uint8_t)v;
    p[1] = (rt_uint8_t)(v >> 8);
}

static void put_u32(rt_uint8_t *p, rt_uint32_t v)
{
    p[0] = (rt_uint8_t)v;
    p[1] = (rt_uint8_t)(v >> 8);
    p[2] = (rt_uint8_t)(v >> 16);
    p[3] = (rt_uint8_t)(v >> 24);
}

static rt_uint32_t bbt_crc32(const rt_uint8_t *data, rt_size_t len)
{
    rt_uint32_t crc = 0xFFFFFFFFU;
    rt_size_t i;
    int bit;

    for (i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

static rt_uint32_t bbt_map_offset(void)
{
    return BBT_HEADER_SIZE + nand_lfs.total;
}

static rt_uint32_t bbt_crc_offset(void)
{
    return bbt_map_offset() + (rt_uint32_t)nand_lfs.logical * 2U;
}

static rt_uint16_t bbt_count_state(rt_uint8_t state)
{
    rt_uint16_t i, count = 0;

    for (i = 0; i < nand_lfs.total; i++)
        if (nand_lfs.state[i] == state)
            count++;
    return count;
}

static rt_uint16_t bbt_take_spare(void)
{
    rt_uint16_t i;

    for (i = 0; i < nand_lfs.total; i++)
    {
        if (nand_lfs.state[i] == BBT_STATE_SPARE &&
            nand_lfs.scan[i] == 0)
        {
            nand_lfs.state[i] = BBT_STATE_MAPPED;
            nand_lfs.spare_count--;
            return i;
        }
    }
    return BBT_NO_BLOCK;
}

static void bbt_serialize(void)
{
    rt_uint32_t map_off = bbt_map_offset();
    rt_uint32_t crc_off = bbt_crc_offset();
    rt_uint16_t i;

    rt_memset(nand_lfs.record, 0xFF, nand_lfs.mtd->page_size);
    rt_memcpy(nand_lfs.record + BBT_OFF_MAGIC, "AICB", 4);
    put_u16(nand_lfs.record + BBT_OFF_VERSION, BBT_VERSION);
    put_u16(nand_lfs.record + BBT_OFF_HEADER_SIZE, BBT_HEADER_SIZE);
    put_u32(nand_lfs.record + BBT_OFF_SEQUENCE, nand_lfs.sequence);
    put_u16(nand_lfs.record + BBT_OFF_TOTAL, nand_lfs.total);
    put_u16(nand_lfs.record + BBT_OFF_LOGICAL, nand_lfs.logical);
    put_u16(nand_lfs.record + BBT_OFF_SLOT0, nand_lfs.slots[0]);
    put_u16(nand_lfs.record + BBT_OFF_SLOT1, nand_lfs.slots[1]);
    put_u16(nand_lfs.record + BBT_OFF_SPARE, nand_lfs.spare_count);
    put_u16(nand_lfs.record + BBT_OFF_RESERVED, 0);

    rt_memcpy(nand_lfs.record + BBT_HEADER_SIZE, nand_lfs.state,
              nand_lfs.total);
    for (i = 0; i < nand_lfs.logical; i++)
        put_u16(nand_lfs.record + map_off + (rt_uint32_t)i * 2U,
                nand_lfs.map[i]);

    put_u32(nand_lfs.record + crc_off,
            bbt_crc32(nand_lfs.record, crc_off));
}

static int bbt_validate(const rt_uint8_t *buf)
{
    rt_uint32_t map_off, crc_off;
    rt_uint16_t total, logical, slot0, slot1, i, j;

    if (rt_memcmp(buf + BBT_OFF_MAGIC, "AICB", 4) != 0)
        return -1;
    if (get_u16(buf + BBT_OFF_VERSION) != BBT_VERSION)
        return -1;
    if (get_u16(buf + BBT_OFF_HEADER_SIZE) != BBT_HEADER_SIZE)
        return -1;

    total = get_u16(buf + BBT_OFF_TOTAL);
    logical = get_u16(buf + BBT_OFF_LOGICAL);
    if (total != nand_lfs.total || logical == 0 || logical > total)
        return -1;

    map_off = BBT_HEADER_SIZE + total;
    crc_off = map_off + (rt_uint32_t)logical * 2U;
    if (crc_off + 4U > nand_lfs.mtd->page_size)
        return -1;
    if (get_u32(buf + crc_off) != bbt_crc32(buf, crc_off))
        return -1;

    slot0 = get_u16(buf + BBT_OFF_SLOT0);
    slot1 = get_u16(buf + BBT_OFF_SLOT1);
    if (slot0 >= total || slot1 >= total || slot0 == slot1)
        return -1;

    for (i = 0; i < total; i++)
    {
        if (buf[BBT_HEADER_SIZE + i] > BBT_STATE_SPARE)
            return -1;
    }
    if (buf[BBT_HEADER_SIZE + slot0] != BBT_STATE_SLOT ||
        buf[BBT_HEADER_SIZE + slot1] != BBT_STATE_SLOT)
        return -1;

    for (i = 0; i < logical; i++)
    {
        rt_uint16_t phys = get_u16(buf + map_off + (rt_uint32_t)i * 2U);

        if (phys >= total)
            return -1;
        if (buf[BBT_HEADER_SIZE + phys] != BBT_STATE_MAPPED)
            return -1;
        for (j = 0; j < i; j++)
        {
            if (get_u16(buf + map_off + (rt_uint32_t)j * 2U) == phys)
                return -1;
        }
    }

    return 0;
}

static void bbt_import(const rt_uint8_t *buf)
{
    rt_uint32_t map_off = bbt_map_offset();
    rt_uint16_t i;

    nand_lfs.sequence = get_u32(buf + BBT_OFF_SEQUENCE);
    nand_lfs.logical = get_u16(buf + BBT_OFF_LOGICAL);
    nand_lfs.slots[0] = get_u16(buf + BBT_OFF_SLOT0);
    nand_lfs.slots[1] = get_u16(buf + BBT_OFF_SLOT1);

    rt_memcpy(nand_lfs.state, buf + BBT_HEADER_SIZE, nand_lfs.total);
    for (i = 0; i < nand_lfs.logical; i++)
        nand_lfs.map[i] = get_u16(buf + map_off + (rt_uint32_t)i * 2U);

    nand_lfs.bad_count = bbt_count_state(BBT_STATE_BAD);
    nand_lfs.spare_count = bbt_count_state(BBT_STATE_SPARE);
}

static int bbt_read_slot(rt_int8_t index, rt_uint8_t *buf)
{
    rt_uint32_t page = (rt_uint32_t)nand_lfs.slots[index] *
                       nand_lfs.mtd->pages_per_block;
    rt_err_t ret;

    ret = rt_mtd_nand_read(nand_lfs.mtd, page, buf,
                           nand_lfs.mtd->page_size, RT_NULL, 0);
    if (ret != RT_MTD_EOK && ret != RT_MTD_EECC_CORRECT)
        return -1;

    return bbt_validate(buf);
}

static int bbt_write_slot(rt_int8_t index)
{
    rt_uint32_t block = nand_lfs.slots[index];
    rt_uint32_t page = block * nand_lfs.mtd->pages_per_block;
    rt_err_t ret;

    ret = rt_mtd_nand_erase_block(nand_lfs.mtd, block);
    if (ret != RT_MTD_EOK)
        return -1;

    ret = rt_mtd_nand_write(nand_lfs.mtd, page, nand_lfs.record,
                            nand_lfs.mtd->page_size, RT_NULL, 0);
    if (ret != RT_MTD_EOK)
        return -1;

    return 0;
}

static int bbt_persist(void)
{
    rt_int8_t target;

    nand_lfs.sequence++;
    bbt_serialize();

    if (nand_lfs.active_slot < 0)
    {
        if (bbt_write_slot(0) == 0)
        {
            nand_lfs.active_slot = 0;
            return 0;
        }
        if (bbt_write_slot(1) == 0)
        {
            nand_lfs.active_slot = 1;
            return 0;
        }
        return -1;
    }

    target = (nand_lfs.active_slot == 0) ? 1 : 0;
    if (bbt_write_slot(target) != 0)
    {
        rt_kprintf("nand_lfs: BBT write to slot %d failed\n", target);
        return -1;
    }

    nand_lfs.active_slot = target;
    return 0;
}

static int bbt_scan_factory(void)
{
    rt_uint16_t i;

    for (i = 0; i < nand_lfs.total; i++)
    {
        rt_err_t ret = rt_mtd_nand_check_block(nand_lfs.mtd, i);

        if (ret == RT_EOK)
            nand_lfs.scan[i] = 0;
        else if (ret == -RT_ERROR || ret == RT_MTD_EECC)
            nand_lfs.scan[i] = 1;
        else
        {
            rt_kprintf("nand_lfs: block %u scan failed (%d)\n", i, ret);
            return -1;
        }
    }
    return 0;
}

static int bbt_find_slots(void)
{
    rt_uint16_t i;
    rt_int8_t found = 0;

    for (i = 0; i < nand_lfs.total && found < 2; i++)
    {
        if (nand_lfs.scan[i] == 0)
            nand_lfs.slots[found++] = i;
    }
    return (found == 2) ? 0 : -1;
}

static int bbt_assign_map(void)
{
    rt_uint16_t i;
    rt_uint16_t good = 0;
    rt_uint16_t spare;
    rt_uint16_t mapped = 0;

    for (i = 0; i < nand_lfs.total; i++)
    {
        if (nand_lfs.scan[i] == 0 &&
            nand_lfs.state[i] != BBT_STATE_SLOT &&
            nand_lfs.state[i] != BBT_STATE_BAD)
            good++;
    }

    if (good < LFS_MIN_LOGICAL_BLOCKS)
    {
        rt_kprintf("nand_lfs: only %u usable blocks, need %u\n",
                   good, LFS_MIN_LOGICAL_BLOCKS);
        return -1;
    }

    spare = (good > LFS_MIN_LOGICAL_BLOCKS)
            ? (rt_uint16_t)BSP_SPINAND_LITTLEFS_SPARE_BLOCKS : 0;
    if (spare > good - LFS_MIN_LOGICAL_BLOCKS)
        spare = good - LFS_MIN_LOGICAL_BLOCKS;

    nand_lfs.logical = good - spare;

    for (i = 0; i < nand_lfs.total; i++)
    {
        if (nand_lfs.scan[i] != 0)
            nand_lfs.state[i] = BBT_STATE_BAD;
        else if (nand_lfs.state[i] == BBT_STATE_SLOT ||
                 nand_lfs.state[i] == BBT_STATE_BAD)
            continue;
        else if (mapped < nand_lfs.logical)
        {
            nand_lfs.state[i] = BBT_STATE_MAPPED;
            nand_lfs.map[mapped++] = i;
        }
        else
        {
            nand_lfs.state[i] = BBT_STATE_SPARE;
        }
    }

    nand_lfs.bad_count = bbt_count_state(BBT_STATE_BAD);
    nand_lfs.spare_count = bbt_count_state(BBT_STATE_SPARE);

    return (mapped == nand_lfs.logical) ? 0 : -1;
}

static int bbt_build_fresh(void)
{
    rt_uint16_t i;

    for (i = 0; i < nand_lfs.total; i++)
        nand_lfs.state[i] = (nand_lfs.scan[i] != 0)
                            ? BBT_STATE_BAD : BBT_STATE_SPARE;

    if (bbt_find_slots() != 0)
        return -1;
    nand_lfs.state[nand_lfs.slots[0]] = BBT_STATE_SLOT;
    nand_lfs.state[nand_lfs.slots[1]] = BBT_STATE_SLOT;

    if (bbt_assign_map() != 0)
        return -1;

    nand_lfs.sequence = 0;
    nand_lfs.active_slot = -1;
    if (bbt_persist() != 0)
        return -1;

    /*
     * bbt_persist() stops after the first copy when no slot is active.
     * Write the alternate copy as well, so a freshly created table already
     * has the redundancy the recovery path depends on.
     */
    if (bbt_persist() != 0)
        return -1;

    return 0;
}

static int bbt_merge_factory(rt_bool_t *changed)
{
    rt_uint16_t i, j;

    *changed = RT_FALSE;

    for (i = 0; i < nand_lfs.total; i++)
    {
        rt_uint16_t replacement;

        if (nand_lfs.scan[i] == 0)
            continue;
        if (nand_lfs.state[i] == BBT_STATE_BAD)
            continue;

        if (nand_lfs.state[i] == BBT_STATE_SPARE)
        {
            nand_lfs.state[i] = BBT_STATE_BAD;
            nand_lfs.bad_count++;
            nand_lfs.spare_count--;
            *changed = RT_TRUE;
            continue;
        }

        replacement = bbt_take_spare();
        if (replacement == BBT_NO_BLOCK)
        {
            rt_kprintf("nand_lfs: block %u died and no spare is left\n", i);
            return -1;
        }

        if (nand_lfs.state[i] == BBT_STATE_SLOT)
        {
            nand_lfs.state[replacement] = BBT_STATE_SLOT;
            if (nand_lfs.slots[0] == i)
                nand_lfs.slots[0] = replacement;
            else
                nand_lfs.slots[1] = replacement;
            rt_kprintf("nand_lfs: BBT slot %u -> %u\n", i, replacement);
        }
        else
        {
            for (j = 0; j < nand_lfs.logical; j++)
            {
                if (nand_lfs.map[j] == i)
                {
                    nand_lfs.map[j] = replacement;
                    rt_kprintf("nand_lfs: logical %u remapped %u -> %u\n",
                               j, i, replacement);
                }
            }
        }

        nand_lfs.state[i] = BBT_STATE_BAD;
        nand_lfs.bad_count++;
        *changed = RT_TRUE;
    }

    return 0;
}

static int bbt_load(rt_bool_t *found)
{
    rt_bool_t valid0, valid1;
    rt_bool_t changed;
    rt_uint32_t seq0 = 0, seq1 = 0;
    const rt_uint8_t *winner;
    rt_int8_t winner_slot;

    *found = RT_FALSE;

    valid0 = (bbt_read_slot(0, nand_lfs.record) == 0);
    valid1 = (bbt_read_slot(1, nand_lfs.readbuf) == 0);
    if (valid0)
        seq0 = get_u32(nand_lfs.record + BBT_OFF_SEQUENCE);
    if (valid1)
        seq1 = get_u32(nand_lfs.readbuf + BBT_OFF_SEQUENCE);

    if (!valid0 && !valid1)
        return 0;

    if (valid0 && valid1)
    {
        if ((rt_int32_t)(seq0 - seq1) >= 0)
        {
            winner = nand_lfs.record;
            winner_slot = 0;
        }
        else
        {
            winner = nand_lfs.readbuf;
            winner_slot = 1;
        }
    }
    else if (valid0)
    {
        winner = nand_lfs.record;
        winner_slot = 0;
    }
    else
    {
        winner = nand_lfs.readbuf;
        winner_slot = 1;
    }

    if (winner != nand_lfs.record)
        rt_memcpy(nand_lfs.record, winner, nand_lfs.mtd->page_size);

    bbt_import(nand_lfs.record);
    nand_lfs.active_slot = winner_slot;

    if (bbt_merge_factory(&changed) != 0)
        return -1;

    if (changed && bbt_persist() != 0)
        rt_kprintf("nand_lfs: failed to persist recovered BBT\n");

    *found = RT_TRUE;
    return 0;
}

int nand_lfs_init(void)
{
    struct rt_device *dev;
    rt_uint32_t required;
    rt_err_t ret;

    if (nand_lfs.ready)
        return RT_EOK;

    dev = rt_device_find(LFS_NAND_MTD_NAME);
    if (dev == RT_NULL)
    {
        rt_kprintf("nand_lfs: MTD '%s' not found\n", LFS_NAND_MTD_NAME);
        return -RT_EIO;
    }
    if (dev->type != RT_Device_Class_MTD)
    {
        rt_kprintf("nand_lfs: '%s' is not an MTD device\n", LFS_NAND_MTD_NAME);
        return -RT_EINVAL;
    }

    nand_lfs.mtd = RT_MTD_NAND_DEVICE(dev);
    nand_lfs.total = nand_lfs.mtd->block_total;
    if (nand_lfs.total < 8 || nand_lfs.mtd->page_size == 0 ||
        nand_lfs.mtd->pages_per_block == 0)
    {
        rt_kprintf("nand_lfs: unsupported geometry blocks=%u page=%u\n",
                   nand_lfs.total, nand_lfs.mtd->page_size);
        return -RT_EINVAL;
    }

    required = BBT_HEADER_SIZE + (rt_uint32_t)nand_lfs.total * 3U + 4U;
    if (required > nand_lfs.mtd->page_size)
    {
        rt_kprintf("nand_lfs: BBT needs %u bytes, page is %u\n",
                   required, nand_lfs.mtd->page_size);
        return -RT_ENOSPC;
    }

    nand_lfs.state = rt_malloc(nand_lfs.total);
    nand_lfs.scan = rt_malloc(nand_lfs.total);
    nand_lfs.map = rt_malloc((rt_size_t)nand_lfs.total * sizeof(rt_uint16_t));
    nand_lfs.record = rt_malloc(nand_lfs.mtd->page_size);
    nand_lfs.readbuf = rt_malloc(nand_lfs.mtd->page_size);
    if (nand_lfs.state == RT_NULL || nand_lfs.scan == RT_NULL ||
        nand_lfs.map == RT_NULL || nand_lfs.record == RT_NULL ||
        nand_lfs.readbuf == RT_NULL)
    {
        ret = -RT_ENOMEM;
        goto fail;
    }

    ret = rt_mutex_init(&nand_lfs.lock, "nandlfs", RT_IPC_FLAG_PRIO);
    if (ret != RT_EOK)
        goto fail;

    rt_mutex_take(&nand_lfs.lock, RT_WAITING_FOREVER);

    if (bbt_scan_factory() != 0)
    {
        ret = -RT_EIO;
        goto fail_unlock;
    }
    if (bbt_find_slots() != 0)
    {
        rt_kprintf("nand_lfs: fewer than two good blocks\n");
        ret = -RT_ENOSPC;
        goto fail_unlock;
    }

    {
        rt_bool_t found;

        if (bbt_load(&found) != 0)
        {
            ret = -RT_EIO;
            goto fail_unlock;
        }
        if (found)
        {
            nand_lfs.created = RT_FALSE;
        }
        else
        {
            if (bbt_build_fresh() != 0)
            {
                ret = -RT_EIO;
                goto fail_unlock;
            }
            nand_lfs.created = RT_TRUE;
            rt_kprintf("nand_lfs: blank scratch, created BBT\n");
        }
    }

    nand_lfs.ready = RT_TRUE;
    rt_mutex_release(&nand_lfs.lock);

    rt_kprintf("nand_lfs: %u blocks, %u logical + %u spare + 2 BBT, "
               "%u bad\n", nand_lfs.total, nand_lfs.logical,
               nand_lfs.spare_count, nand_lfs.bad_count);
    return RT_EOK;

fail_unlock:
    rt_mutex_release(&nand_lfs.lock);
    rt_mutex_detach(&nand_lfs.lock);
fail:
    rt_free(nand_lfs.state);
    rt_free(nand_lfs.scan);
    rt_free(nand_lfs.map);
    rt_free(nand_lfs.record);
    rt_free(nand_lfs.readbuf);
    nand_lfs.state = RT_NULL;
    nand_lfs.scan = RT_NULL;
    nand_lfs.map = RT_NULL;
    nand_lfs.record = RT_NULL;
    nand_lfs.readbuf = RT_NULL;
    return ret;
}

static void nand_lfs_mark_bad(lfs_block_t block)
{
    rt_uint16_t phys;
    rt_uint16_t replacement;

    if (block >= nand_lfs.logical)
        return;

    phys = nand_lfs.map[block];
    if (phys == BBT_NO_BLOCK || nand_lfs.state[phys] == BBT_STATE_BAD)
        return;

    nand_lfs.state[phys] = BBT_STATE_BAD;
    nand_lfs.bad_count++;

    replacement = bbt_take_spare();
    if (replacement != BBT_NO_BLOCK)
    {
        nand_lfs.map[block] = replacement;
        rt_kprintf("nand_lfs: logical %u remapped %u -> %u\n",
                   (unsigned int)block, phys, replacement);
    }
    else
    {
        nand_lfs.map[block] = BBT_NO_BLOCK;
        rt_kprintf("nand_lfs: logical %u lost, no spare left\n",
                   (unsigned int)block);
    }

    if (bbt_persist() != 0)
        rt_kprintf("nand_lfs: failed to persist bad-block update\n");
}

static int lfs_err_to_dfs(int err)
{
    switch (err)
    {
    case 0:
        return 0;
    case LFS_ERR_IO:
    case LFS_ERR_CORRUPT:
        return -EIO;
    case LFS_ERR_NOENT:
        return -ENOENT;
    case LFS_ERR_EXIST:
        return -EEXIST;
    case LFS_ERR_NOTDIR:
        return -ENOTDIR;
    case LFS_ERR_ISDIR:
        return -EISDIR;
    case LFS_ERR_INVAL:
        return -EINVAL;
    case LFS_ERR_NOSPC:
        return -ENOSPC;
    case LFS_ERR_NOMEM:
        return -ENOMEM;
    case LFS_ERR_NAMETOOLONG:
        return -ENAMETOOLONG;
    default:
        return -EIO;
    }
}

static int lfs_nand_read_cb(const struct lfs_config *c, lfs_block_t block,
                            lfs_off_t off, void *buffer, lfs_size_t size)
{
    struct nand_lfs *nl = (struct nand_lfs *)c->context;
    rt_uint8_t *dst = (rt_uint8_t *)buffer;
    rt_uint32_t page_size = nl->mtd->page_size;
    rt_uint16_t phys;
    rt_uint32_t pages, i;

    if (block >= nl->logical || off + size > c->block_size ||
        (off % page_size) != 0 || (size % page_size) != 0)
        return LFS_ERR_INVAL;

    phys = nl->map[block];
    if (phys == BBT_NO_BLOCK)
        return LFS_ERR_IO;

    pages = size / page_size;
    for (i = 0; i < pages; i++)
    {
        rt_uint32_t page = (rt_uint32_t)phys * nl->mtd->pages_per_block +
                           (rt_uint32_t)(off / page_size) + i;
        rt_err_t ret = rt_mtd_nand_read(nl->mtd, page, dst + i * page_size,
                                        page_size, RT_NULL, 0);

        if (ret == RT_MTD_EOK || ret == RT_MTD_EECC_CORRECT)
            continue;
        if (ret == RT_MTD_EECC)
        {
            rt_kprintf("nand_lfs: uncorrectable ECC at logical %u\n",
                       (unsigned int)block);
            return LFS_ERR_CORRUPT;
        }
        return LFS_ERR_IO;
    }

    return 0;
}

static int lfs_nand_prog_cb(const struct lfs_config *c, lfs_block_t block,
                            lfs_off_t off, const void *buffer, lfs_size_t size)
{
    struct nand_lfs *nl = (struct nand_lfs *)c->context;
    const rt_uint8_t *src = (const rt_uint8_t *)buffer;
    rt_uint32_t page_size = nl->mtd->page_size;
    rt_uint16_t phys;
    rt_uint32_t pages, i;

    if (block >= nl->logical || off + size > c->block_size ||
        (off % page_size) != 0 || (size % page_size) != 0)
        return LFS_ERR_INVAL;

    phys = nl->map[block];
    if (phys == BBT_NO_BLOCK)
        return LFS_ERR_IO;

    pages = size / page_size;
    for (i = 0; i < pages; i++)
    {
        rt_uint32_t page = (rt_uint32_t)phys * nl->mtd->pages_per_block +
                           (rt_uint32_t)(off / page_size) + i;
        rt_err_t ret = rt_mtd_nand_write(nl->mtd, page, src + i * page_size,
                                         page_size, RT_NULL, 0);

        if (ret != RT_MTD_EOK)
        {
            rt_kprintf("nand_lfs: program failed at logical %u (%d)\n",
                       (unsigned int)block, ret);
            nand_lfs_mark_bad(block);
            return LFS_ERR_IO;
        }
    }

    return 0;
}

static int lfs_nand_erase_cb(const struct lfs_config *c, lfs_block_t block)
{
    struct nand_lfs *nl = (struct nand_lfs *)c->context;
    rt_uint16_t phys;
    rt_err_t ret;

    if (block >= nl->logical)
        return LFS_ERR_INVAL;

    phys = nl->map[block];
    if (phys == BBT_NO_BLOCK)
        return LFS_ERR_IO;

    ret = rt_mtd_nand_erase_block(nl->mtd, phys);
    if (ret == RT_MTD_EOK)
        return 0;

    rt_kprintf("nand_lfs: erase failed at logical %u (%d)\n",
               (unsigned int)block, ret);
    nand_lfs_mark_bad(block);
    phys = nl->map[block];
    if (phys == BBT_NO_BLOCK)
        return LFS_ERR_IO;

    ret = rt_mtd_nand_erase_block(nl->mtd, phys);
    return (ret == RT_MTD_EOK) ? 0 : LFS_ERR_IO;
}

static int lfs_nand_sync_cb(const struct lfs_config *c)
{
    (void)c;
    return 0;
}

static void nand_lfs_fill_config(void)
{
    rt_memset(&lfs_cfg, 0, sizeof(lfs_cfg));

    lfs_cfg.context = &nand_lfs;
    lfs_cfg.read = lfs_nand_read_cb;
    lfs_cfg.prog = lfs_nand_prog_cb;
    lfs_cfg.erase = lfs_nand_erase_cb;
    lfs_cfg.sync = lfs_nand_sync_cb;
    lfs_cfg.read_size = nand_lfs.mtd->page_size;
    lfs_cfg.prog_size = nand_lfs.mtd->page_size;
    lfs_cfg.block_size = nand_lfs.mtd->pages_per_block *
                         nand_lfs.mtd->page_size;
    lfs_cfg.block_count = nand_lfs.logical;
    lfs_cfg.block_cycles = LFS_BLOCK_CYCLES;
    lfs_cfg.cache_size = nand_lfs.mtd->page_size;
    lfs_cfg.lookahead_size = LFS_LOOKAHEAD_SIZE;
}

/*
 * LittleFS is not thread-safe by default.  Every adapter entry point
 * serialises on the BBT mutex; RT-Thread mutexes are recursive, so the
 * bad-block path may take the same mutex again while a filesystem
 * operation is in progress.
 */
static void lfs_api_lock(void)
{
    rt_mutex_take(&nand_lfs.lock, RT_WAITING_FOREVER);
}

static void lfs_api_unlock(void)
{
    rt_mutex_release(&nand_lfs.lock);
}

static int dfs_lfs_mount(struct dfs_filesystem *fs, unsigned long rwflag,
                         const void *data)
{
    int ret;

    (void)rwflag;
    (void)data;

    if (bbt_reboot_required)
    {
        rt_kprintf("nand_lfs: BBT slot was invalidated; reboot before "
                   "mounting %s\n", LFS_NAND_MOUNT_POINT);
        return -EIO;
    }

    ret = nand_lfs_init();
    if (ret != RT_EOK)
        return lfs_err_to_dfs(-EIO);

    if (lfs_is_mounted)
        return -EBUSY;

    nand_lfs_fill_config();
    lfs_api_lock();

    ret = lfs_mount(&lfs_fs, &lfs_cfg);
#ifdef BSP_SPINAND_LITTLEFS_AUTOFORMAT
    if (ret != 0 && nand_lfs.created)
    {
        rt_kprintf("nand_lfs: no filesystem found, formatting scratch\n");
        ret = lfs_format(&lfs_fs, &lfs_cfg);
        if (ret == 0)
            ret = lfs_mount(&lfs_fs, &lfs_cfg);
        else
            rt_kprintf("nand_lfs: format failed (%d)\n", ret);
    }
#endif

    if (ret != 0)
    {
        lfs_api_unlock();
        rt_kprintf("nand_lfs: mount failed (%d)\n", ret);
        return lfs_err_to_dfs(ret);
    }

    lfs_is_mounted = RT_TRUE;
    fs->data = &lfs_fs;
    lfs_api_unlock();
    rt_kprintf("nand_lfs: mounted at %s (%u blocks of %u KiB)\n",
               LFS_NAND_MOUNT_POINT, nand_lfs.logical,
               lfs_cfg.block_size / 1024U);
    return 0;
}

static int dfs_lfs_unmount(struct dfs_filesystem *fs)
{
    int ret;

    if (!lfs_is_mounted)
        return -EINVAL;

    lfs_api_lock();
    ret = lfs_unmount(&lfs_fs);
    if (ret == 0)
    {
        lfs_is_mounted = RT_FALSE;
        fs->data = RT_NULL;
    }
    lfs_api_unlock();

    return (ret == 0) ? 0 : lfs_err_to_dfs(ret);
}

static int dfs_lfs_mkfs(rt_device_t dev_id, const char *fs_name)
{
    int ret;

    (void)dev_id;
    (void)fs_name;

    if (lfs_is_mounted)
        return -EBUSY;
    if (bbt_reboot_required)
    {
        rt_kprintf("nand_lfs: BBT slot was invalidated; reboot before "
                   "formatting %s\n", LFS_NAND_MOUNT_POINT);
        return -EIO;
    }

    ret = nand_lfs_init();
    if (ret != RT_EOK)
        return -EIO;

    nand_lfs_fill_config();
    lfs_api_lock();
    ret = lfs_format(&lfs_fs, &lfs_cfg);
    if (ret == 0)
        nand_lfs.created = RT_FALSE;
    lfs_api_unlock();

    return (ret == 0) ? 0 : lfs_err_to_dfs(ret);
}

static int dfs_lfs_statfs(struct dfs_filesystem *fs, struct statfs *buf)
{
    lfs_ssize_t used;

    (void)fs;

    if (!lfs_is_mounted)
        return -EINVAL;

    lfs_api_lock();
    used = lfs_fs_size(&lfs_fs);
    lfs_api_unlock();
    if (used < 0)
        return lfs_err_to_dfs((int)used);

    buf->f_bsize = lfs_cfg.block_size;
    buf->f_blocks = lfs_cfg.block_count;
    buf->f_bfree = lfs_cfg.block_count - (lfs_size_t)used;
    buf->f_bavail = buf->f_bfree;
    return 0;
}

static int dfs_lfs_unlink(struct dfs_filesystem *fs, const char *pathname)
{
    int ret;

    (void)fs;

    lfs_api_lock();
    ret = lfs_remove(&lfs_fs, pathname);
    lfs_api_unlock();

    return lfs_err_to_dfs(ret);
}

static int dfs_lfs_stat(struct dfs_filesystem *fs, const char *filename,
                        struct stat *buf)
{
    struct lfs_info info;
    int ret;

    (void)fs;

    lfs_api_lock();
    ret = lfs_stat(&lfs_fs, filename, &info);
    lfs_api_unlock();
    if (ret != 0)
        return lfs_err_to_dfs(ret);

    rt_memset(buf, 0, sizeof(*buf));
    buf->st_dev = 0;
    if (info.type == LFS_TYPE_DIR)
        buf->st_mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH |
                       S_IWUSR | S_IWGRP | S_IWOTH;
    else
        buf->st_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH |
                       S_IWUSR | S_IWGRP | S_IWOTH;
    buf->st_size = info.size;
    return 0;
}

static int dfs_lfs_rename(struct dfs_filesystem *fs, const char *oldpath,
                          const char *newpath)
{
    int ret;

    (void)fs;

    lfs_api_lock();
    ret = lfs_rename(&lfs_fs, oldpath, newpath);
    lfs_api_unlock();

    return lfs_err_to_dfs(ret);
}

struct lfs_dfs_object
{
    union
    {
        lfs_file_t file;
        lfs_dir_t dir;
    } u;
    rt_bool_t is_dir;
};

static const char *lfs_dfs_path(struct dfs_file *fd)
{
    const char *path = fd->vnode->path;

    if (path == RT_NULL)
        return "";
    while (*path == '/')
        path++;
    return path;
}

static int dfs_lfs_open(struct dfs_file *fd)
{
    struct lfs_dfs_object *obj;
    const char *path = lfs_dfs_path(fd);
    int ret;

    obj = rt_malloc(sizeof(*obj));
    if (obj == RT_NULL)
        return -ENOMEM;
    rt_memset(obj, 0, sizeof(*obj));

    lfs_api_lock();
    if (fd->flags & O_DIRECTORY)
    {
        if (fd->flags & O_CREAT)
            lfs_mkdir(&lfs_fs, path);

        ret = lfs_dir_open(&lfs_fs, &obj->u.dir, path);
        obj->is_dir = RT_TRUE;
    }
    else
    {
        int flags;

        switch (fd->flags & O_ACCMODE)
        {
        case O_WRONLY:
            flags = LFS_O_WRONLY;
            break;
        case O_RDWR:
            flags = LFS_O_RDWR;
            break;
        default:
            flags = LFS_O_RDONLY;
            break;
        }
        if (fd->flags & O_CREAT)
            flags |= LFS_O_CREAT;
        if (fd->flags & O_EXCL)
            flags |= LFS_O_EXCL;
        if (fd->flags & O_TRUNC)
            flags |= LFS_O_TRUNC;
        if (fd->flags & O_APPEND)
            flags |= LFS_O_APPEND;

        ret = lfs_file_open(&lfs_fs, &obj->u.file, path, flags);
        obj->is_dir = RT_FALSE;
    }

    if (ret == 0 && !obj->is_dir)
    {
        lfs_soff_t size = lfs_file_size(&lfs_fs, &obj->u.file);

        if (size >= 0)
            fd->vnode->size = (size_t)size;
    }
    lfs_api_unlock();

    if (ret != 0)
    {
        rt_free(obj);
        return lfs_err_to_dfs(ret);
    }

    fd->data = obj;
    return 0;
}

static int dfs_lfs_close(struct dfs_file *fd)
{
    struct lfs_dfs_object *obj = (struct lfs_dfs_object *)fd->data;
    int ret;

    if (obj == RT_NULL)
        return 0;

    lfs_api_lock();
    if (obj->is_dir)
        ret = lfs_dir_close(&lfs_fs, &obj->u.dir);
    else
        ret = lfs_file_close(&lfs_fs, &obj->u.file);
    lfs_api_unlock();

    rt_free(obj);
    fd->data = RT_NULL;
    return lfs_err_to_dfs(ret);
}

static ssize_t dfs_lfs_read(struct dfs_file *fd, void *buf, size_t count)
{
    struct lfs_dfs_object *obj = (struct lfs_dfs_object *)fd->data;
    lfs_ssize_t ret;

    if (obj == RT_NULL)
        return -EBADF;
    if (obj->is_dir)
        return -EISDIR;
    if ((rt_uint64_t)count > 0x7FFFFFFFULL)
        return -EINVAL;

    lfs_api_lock();
    ret = lfs_file_read(&lfs_fs, &obj->u.file, buf, (lfs_size_t)count);
    if (ret >= 0)
        fd->pos = lfs_file_tell(&lfs_fs, &obj->u.file);
    lfs_api_unlock();

    return (ret < 0) ? lfs_err_to_dfs((int)ret) : (ssize_t)ret;
}

static ssize_t dfs_lfs_write(struct dfs_file *fd, const void *buf,
                             size_t count)
{
    struct lfs_dfs_object *obj = (struct lfs_dfs_object *)fd->data;
    lfs_ssize_t ret;

    if (obj == RT_NULL)
        return -EBADF;
    if (obj->is_dir)
        return -EISDIR;
    if ((rt_uint64_t)count > 0x7FFFFFFFULL)
        return -EINVAL;

    lfs_api_lock();
    ret = lfs_file_write(&lfs_fs, &obj->u.file, buf, (lfs_size_t)count);
    if (ret >= 0)
    {
        lfs_soff_t size;

        fd->pos = lfs_file_tell(&lfs_fs, &obj->u.file);
        size = lfs_file_size(&lfs_fs, &obj->u.file);
        if (size >= 0)
            fd->vnode->size = (size_t)size;
    }
    lfs_api_unlock();

    return (ret < 0) ? lfs_err_to_dfs((int)ret) : (ssize_t)ret;
}

static int dfs_lfs_flush(struct dfs_file *fd)
{
    struct lfs_dfs_object *obj = (struct lfs_dfs_object *)fd->data;
    int ret;

    if (obj == RT_NULL)
        return -EBADF;
    if (obj->is_dir)
        return -EISDIR;

    lfs_api_lock();
    ret = lfs_file_sync(&lfs_fs, &obj->u.file);
    lfs_api_unlock();

    return lfs_err_to_dfs(ret);
}

static off_t dfs_lfs_lseek(struct dfs_file *fd, off_t offset)
{
    struct lfs_dfs_object *obj = (struct lfs_dfs_object *)fd->data;
    lfs_soff_t ret;

    if (obj == RT_NULL)
        return -EBADF;
    if (obj->is_dir)
        return -EISDIR;
    if (offset < 0 || (rt_uint64_t)offset > 0x7FFFFFFFULL)
        return -EINVAL;

    lfs_api_lock();
    ret = lfs_file_seek(&lfs_fs, &obj->u.file,
                        (lfs_soff_t)offset, LFS_SEEK_SET);
    lfs_api_unlock();

    return (ret < 0) ? lfs_err_to_dfs((int)ret) : (off_t)ret;
}

static int dfs_lfs_getdents(struct dfs_file *fd, struct dirent *dirp,
                            uint32_t count)
{
    struct lfs_dfs_object *obj = (struct lfs_dfs_object *)fd->data;
    struct lfs_info info;
    rt_uint32_t index = 0;
    int ret;

    if (obj == RT_NULL || !obj->is_dir)
        return -ENOTDIR;

    count = (count / sizeof(struct dirent)) * sizeof(struct dirent);
    if (count == 0)
        return -EINVAL;

    lfs_api_lock();
    while (1)
    {
        ret = lfs_dir_read(&lfs_fs, &obj->u.dir, &info);
        if (ret < 0)
        {
            if (index == 0)
            {
                lfs_api_unlock();
                return lfs_err_to_dfs(ret);
            }
            break;
        }
        if (ret == 0)
            break;

        if (rt_strcmp(info.name, ".") == 0 ||
            rt_strcmp(info.name, "..") == 0)
            continue;

        rt_memset(&dirp[index], 0, sizeof(struct dirent));
        dirp[index].d_type = (info.type == LFS_TYPE_DIR) ? DT_DIR : DT_REG;
        dirp[index].d_namlen = (rt_uint8_t)rt_strlen(info.name);
        dirp[index].d_reclen = (rt_uint16_t)sizeof(struct dirent);
        rt_strncpy(dirp[index].d_name, info.name, DIRENT_NAME_MAX);

        index++;
        if (index * sizeof(struct dirent) >= count)
            break;
    }
    lfs_api_unlock();

    if (index == 0)
        return 0;

    fd->pos += index * sizeof(struct dirent);
    return (int)(index * sizeof(struct dirent));
}

static const struct dfs_file_ops dfs_lfs_fops =
{
    dfs_lfs_open,
    dfs_lfs_close,
    RT_NULL,
    dfs_lfs_read,
    dfs_lfs_write,
    dfs_lfs_flush,
    dfs_lfs_lseek,
    dfs_lfs_getdents,
    RT_NULL,
};

static const struct dfs_filesystem_ops dfs_lfs =
{
    "lfs",
    DFS_FS_FLAG_DEFAULT,
    &dfs_lfs_fops,
    dfs_lfs_mount,
    dfs_lfs_unmount,
    dfs_lfs_mkfs,
    dfs_lfs_statfs,
    dfs_lfs_unlink,
    dfs_lfs_stat,
    dfs_lfs_rename,
};

int aic_nand_lfs_mount(void)
{
    if (dfs_mount(RT_NULL, LFS_NAND_MOUNT_POINT, "lfs", 0, RT_NULL) != 0)
        return -RT_ERROR;

    return RT_EOK;
}

static int nand_lfs_register(void)
{
    return dfs_register(&dfs_lfs);
}
INIT_COMPONENT_EXPORT(nand_lfs_register);

int aic_nand_lfs_get_info(struct aic_nand_lfs_info *info)
{
    if (info == RT_NULL)
        return -RT_EINVAL;
    if (!nand_lfs.ready)
        return -RT_EIO;

    rt_memset(info, 0, sizeof(*info));
    lfs_api_lock();
    info->total = nand_lfs.total;
    info->logical = nand_lfs.logical;
    info->spare = nand_lfs.spare_count;
    info->bad = nand_lfs.bad_count;
    info->slot[0] = nand_lfs.slots[0];
    info->slot[1] = nand_lfs.slots[1];
    info->sequence = nand_lfs.sequence;
    info->mounted = lfs_is_mounted;
    lfs_api_unlock();

    return RT_EOK;
}

static void nand_lfs_info(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (!nand_lfs.ready)
    {
        rt_kprintf("nand_lfs: not initialised\n");
        return;
    }

    rt_kprintf("device %s: %u blocks of %u bytes\n", LFS_NAND_MTD_NAME,
               nand_lfs.total, nand_lfs.mtd->page_size *
               nand_lfs.mtd->pages_per_block);
    rt_kprintf("blocks: %u logical, %u spare, 2 BBT, %u bad\n",
               nand_lfs.logical, nand_lfs.spare_count, nand_lfs.bad_count);
    rt_kprintf("BBT: seq %u, slots %u/%u, active %d\n",
               nand_lfs.sequence, nand_lfs.slots[0], nand_lfs.slots[1],
               nand_lfs.active_slot);
    if (bbt_reboot_required)
        rt_kprintf("BBT test: active slot erased; reboot to recover from "
                   "the other slot\n");
    if (lfs_is_mounted)
    {
        lfs_ssize_t used;

        lfs_api_lock();
        used = lfs_fs_size(&lfs_fs);
        lfs_api_unlock();
        rt_kprintf("littlefs: mounted at %s, used %ld/%u blocks\n",
                   LFS_NAND_MOUNT_POINT, (long)used, nand_lfs.logical);
    }
    else
    {
        rt_kprintf("littlefs: not mounted\n");
    }
}
MSH_CMD_EXPORT_ALIAS(nand_lfs_info, nand_lfs_info,
                     show LittleFS and bad-block table state);

static void nand_lfs_format(int argc, char **argv)
{
    if (argc != 2 || rt_strcmp(argv[1], "confirm") != 0)
    {
        rt_kprintf("usage: nand_lfs_format confirm\n"
                   "This erases all data in %s.\n", LFS_NAND_MOUNT_POINT);
        return;
    }

    if (lfs_is_mounted)
    {
        rt_kprintf("nand_lfs: unmount %s first\n", LFS_NAND_MOUNT_POINT);
        return;
    }

    if (dfs_mkfs("lfs", LFS_NAND_MTD_NAME) == 0)
        rt_kprintf("nand_lfs: formatted, mount %s to use it\n",
                   LFS_NAND_MOUNT_POINT);
    else
        rt_kprintf("nand_lfs: format failed\n");
}
MSH_CMD_EXPORT_ALIAS(nand_lfs_format, nand_lfs_format,
                     format /nand (requires the 'confirm' argument));

/*
 * Destructive BBT recovery test: erase the active BBT slot and require a
 * reboot.  The other slot is left untouched, so the next boot must load
 * the previous sequence from it.  This only touches the two BBT blocks
 * inside nand_scratch, never an image_cfg partition.
 */
static void nand_lfs_bbt_test(int argc, char **argv)
{
    rt_int8_t slot;
    rt_uint16_t block;
    rt_uint32_t seq;
    rt_bool_t other_valid;
    rt_err_t ret;

    if (!nand_lfs.ready)
    {
        rt_kprintf("nand_lfs: not initialised\n");
        return;
    }

    lfs_api_lock();
    slot = nand_lfs.active_slot;
    seq = nand_lfs.sequence;
    block = (slot >= 0) ? nand_lfs.slots[slot] : 0;
    other_valid = (slot >= 0) &&
                  (bbt_read_slot(slot ^ 1, nand_lfs.readbuf) == 0);
    lfs_api_unlock();

    if (argc != 2 || rt_strcmp(argv[1], "confirm") != 0)
    {
        rt_kprintf("usage: nand_lfs_bbt_test confirm\n"
                   "  active BBT slot %d (block %u), sequence %u\n"
                   "  This erases only the active slot and leaves the other\n"
                   "  copy intact.  Reboot afterwards and run nand_lfs_info;\n"
                   "  the recovered sequence must be one lower.\n",
                   slot, block, seq);
        return;
    }

    if (bbt_reboot_required)
    {
        rt_kprintf("nand_lfs: a slot is already invalidated; reboot first\n");
        return;
    }
    if (slot < 0)
    {
        rt_kprintf("nand_lfs: no active BBT slot\n");
        return;
    }
    if (!other_valid)
    {
        rt_kprintf("nand_lfs: slot %d has no valid BBT copy; refusing to "
                   "erase the only table\n", slot ^ 1);
        return;
    }

    if (lfs_is_mounted && dfs_unmount(LFS_NAND_MOUNT_POINT) != 0)
    {
        rt_kprintf("nand_lfs: cannot unmount %s; close open files first\n",
                   LFS_NAND_MOUNT_POINT);
        return;
    }

    ret = rt_mtd_nand_erase_block(nand_lfs.mtd, block);
    if (ret != RT_MTD_EOK)
    {
        rt_kprintf("nand_lfs: erase slot %d (block %u) failed (%d)\n",
                   slot, block, ret);
        return;
    }

    bbt_reboot_required = RT_TRUE;
    rt_kprintf("nand_lfs: BBT slot %d (block %u, seq %u) erased; "
               "reboot to recover from slot %d\n",
               slot, block, seq, slot ^ 1);
}
MSH_CMD_EXPORT_ALIAS(nand_lfs_bbt_test, nand_lfs_bbt_test,
                     erase the active BBT slot to test recovery);

/*
 * Write loop for reset injection: start a watchdog and run this for a
 * few seconds, then reboot and check that /nand still mounts.  Only
 * /nand/stress.bin is written.
 */
#define LFS_STRESS_PATH         aic_board_storage.lfs_stress_path
#define LFS_STRESS_MAGIC        0x4C465358U  /* "LFSX" */
#define LFS_STRESS_DATA_SIZE    4096U

struct lfs_stress_hdr
{
    rt_uint32_t magic;
    rt_uint32_t seq;
    rt_uint32_t data_size;
    rt_uint32_t reserved;
};

static void nand_lfs_stress(int argc, char **argv)
{
    struct dfs_file fd;
    struct lfs_stress_hdr hdr;
    rt_uint8_t *buf;
    rt_tick_t began;
    rt_uint32_t seq = 0;
    int seconds, ret;

    if (!lfs_is_mounted)
    {
        rt_kprintf("nand_lfs_stress: mount %s first\n",
                   LFS_NAND_MOUNT_POINT);
        return;
    }

    if (argc >= 2 && rt_strcmp(argv[1], "check") == 0)
    {
        rt_uint8_t *chk = RT_NULL;
        rt_uint32_t i;
        rt_bool_t payload_ok = RT_TRUE;

        rt_memset(&hdr, 0, sizeof(hdr));
        fd_init(&fd);
        if (dfs_file_open(&fd, LFS_STRESS_PATH, O_RDONLY) < 0)
        {
            rt_kprintf("nand_lfs_stress: %s is missing\n", LFS_STRESS_PATH);
            return;
        }
        ret = (int)dfs_file_read(&fd, &hdr, sizeof(hdr));
        if (ret != (int)sizeof(hdr) || hdr.magic != LFS_STRESS_MAGIC ||
            hdr.data_size != LFS_STRESS_DATA_SIZE)
        {
            dfs_file_close(&fd);
            rt_kprintf("nand_lfs_stress: header incomplete or invalid "
                       "(read %d, data %u)\n", ret, hdr.data_size);
            return;
        }
        chk = (rt_uint8_t *)rt_malloc(hdr.data_size);
        if (chk == RT_NULL)
        {
            dfs_file_close(&fd);
            rt_kprintf("nand_lfs_stress: out of memory\n");
            return;
        }
        ret = (int)dfs_file_read(&fd, chk, hdr.data_size);
        dfs_file_close(&fd);
        if (ret == (int)hdr.data_size)
        {
            for (i = 0; i < hdr.data_size; i++)
            {
                if (chk[i] != 0x5A)
                {
                    payload_ok = RT_FALSE;
                    break;
                }
            }
        }
        else
        {
            payload_ok = RT_FALSE;
        }
        rt_free(chk);
        rt_kprintf("nand_lfs_stress: magic ok, seq %u, data %u bytes, "
                   "payload %s\n", hdr.seq, hdr.data_size,
                   payload_ok ? "ok" : "MISMATCH");
        return;
    }

    if (argc < 2 || rt_strcmp(argv[1], "start") != 0)
    {
        rt_kprintf("usage: nand_lfs_stress start [seconds]\n"
                   "       nand_lfs_stress check\n"
                   "  start writes %s until the watchdog resets the board;\n"
                   "  check inspects the header after the reboot.\n",
                   LFS_STRESS_PATH);
        return;
    }

    seconds = (argc > 2) ? atoi(argv[2]) : 10;
    if (seconds <= 0)
        seconds = 10;

    buf = (rt_uint8_t *)rt_malloc(LFS_STRESS_DATA_SIZE);
    if (buf == RT_NULL)
    {
        rt_kprintf("nand_lfs_stress: out of memory\n");
        return;
    }
    rt_memset(buf, 0x5A, LFS_STRESS_DATA_SIZE);

    began = rt_tick_get();
    while ((rt_tick_get() - began) <
           (rt_tick_t)seconds * RT_TICK_PER_SECOND)
    {
        hdr.magic = LFS_STRESS_MAGIC;
        hdr.seq = seq;
        hdr.data_size = LFS_STRESS_DATA_SIZE;
        hdr.reserved = 0;

        fd_init(&fd);
        if (dfs_file_open(&fd, LFS_STRESS_PATH,
                          O_WRONLY | O_CREAT | O_TRUNC) < 0)
            break;

        ret = (int)dfs_file_write(&fd, &hdr, sizeof(hdr));
        if (ret == (int)sizeof(hdr))
            ret = (int)dfs_file_write(&fd, buf, LFS_STRESS_DATA_SIZE);
        if (dfs_file_flush(&fd) != 0)
            ret = -1;
        dfs_file_close(&fd);

        if (ret != (int)LFS_STRESS_DATA_SIZE)
            break;
        seq++;
    }

    rt_free(buf);
    rt_kprintf("nand_lfs_stress: wrote %u records in %d s\n", seq, seconds);
}
MSH_CMD_EXPORT_ALIAS(nand_lfs_stress, nand_lfs_stress,
                     write /nand/stress.bin for reset-injection testing);
