/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Read-only block device over the 'rodata' SPI NAND partition.
 *
 * The vendor image stores rodata as a FAT filesystem written directly at
 * the start of the partition.  The partition is exposed by drv_spinand.c
 * as a read-only MTD device; this adapter presents it as a 512-byte
 * sector block device so the stock elm-chan FAT filesystem can mount it.
 *
 * There is deliberately no write callback: the block device rejects
 * writes, and the underlying MTD device also rejects program/erase, so a
 * mounted filesystem cannot modify the image even by accident.
 */

#include <rtthread.h>
#include "aic_board.h"
#include <rtdevice.h>
#include <dfs_fs.h>
#include <string.h>

#define RODATA_DEV_NAME     aic_board_storage.rodata_partition
#define RODATA_BLK_NAME     aic_board_storage.rodata_device
#define RODATA_SECTOR_SIZE  512U

struct nand_rodata
{
    struct rt_device parent;
    struct rt_device *mtd_dev;
    struct rt_mtd_nand_device *mtd;
    rt_uint8_t *page_buf;
    rt_uint32_t cached_page;
    rt_uint32_t sector_count;
    rt_uint32_t block_size;
    struct rt_mutex lock;
    rt_bool_t ready;
};

static struct nand_rodata rodata;

static rt_err_t rodata_blk_open(rt_device_t dev, rt_uint16_t oflag)
{
    (void)dev;
    (void)oflag;
    return RT_EOK;
}

static rt_err_t rodata_blk_close(rt_device_t dev)
{
    (void)dev;
    return RT_EOK;
}

static rt_ssize_t rodata_blk_read(rt_device_t dev, rt_off_t pos,
                                  void *buffer, rt_size_t size)
{
    struct nand_rodata *rd = (struct nand_rodata *)dev;
    rt_uint8_t *dst = (rt_uint8_t *)buffer;
    rt_uint64_t sector = (rt_uint64_t)pos;
    rt_size_t done = 0;
    rt_err_t ret;

    if (rd == RT_NULL || !rd->ready || buffer == RT_NULL || size == 0)
        return 0;
    if (sector >= rd->sector_count)
        return 0;
    if (size > rd->sector_count - (rt_uint32_t)sector)
        size = rd->sector_count - (rt_uint32_t)sector;

    if (rt_mutex_take(&rd->lock, RT_WAITING_FOREVER) != RT_EOK)
        return 0;

    while (done < size)
    {
        rt_uint64_t byte = (sector + done) * RODATA_SECTOR_SIZE;
        rt_uint32_t page = (rt_uint32_t)(byte / rd->mtd->page_size);
        rt_uint32_t offset = (rt_uint32_t)(byte % rd->mtd->page_size);
        rt_uint32_t chunk = rd->mtd->page_size - offset;
        rt_size_t remain = (size - done) * RODATA_SECTOR_SIZE;

        if (chunk > remain)
            chunk = (rt_uint32_t)remain;

        if (rd->cached_page != page)
        {
            ret = rt_mtd_nand_read(rd->mtd, page, rd->page_buf,
                                   rd->mtd->page_size, RT_NULL, 0);
            if (ret != RT_MTD_EOK && ret != RT_MTD_EECC_CORRECT)
            {
                rd->cached_page = 0xFFFFFFFFU;
                break;
            }
            rd->cached_page = page;
        }

        rt_memcpy(dst + done * RODATA_SECTOR_SIZE,
                  rd->page_buf + offset, chunk);
        done += chunk / RODATA_SECTOR_SIZE;
    }

    rt_mutex_release(&rd->lock);
    return done;
}

static rt_ssize_t rodata_blk_write(rt_device_t dev, rt_off_t pos,
                                   const void *buffer, rt_size_t size)
{
    (void)dev;
    (void)pos;
    (void)buffer;
    (void)size;
    return 0;
}

static rt_err_t rodata_blk_control(rt_device_t dev, int cmd, void *args)
{
    struct nand_rodata *rd = (struct nand_rodata *)dev;

    switch (cmd)
    {
    case RT_DEVICE_CTRL_BLK_GETGEOME:
        if (args == RT_NULL)
            return -RT_EINVAL;
        {
            struct rt_device_blk_geometry *geometry =
                (struct rt_device_blk_geometry *)args;
            geometry->sector_count = rd->sector_count;
            geometry->bytes_per_sector = RODATA_SECTOR_SIZE;
            geometry->block_size = rd->block_size;
        }
        return RT_EOK;

    case RT_DEVICE_CTRL_BLK_SYNC:
        return RT_EOK;

    case RT_DEVICE_CTRL_BLK_ERASE:
        return -RT_EPERM;

    default:
        return -RT_ENOSYS;
    }
}

#ifdef RT_USING_DEVICE_OPS
static const struct rt_device_ops rodata_blk_ops =
{
    RT_NULL,
    rodata_blk_open,
    rodata_blk_close,
    rodata_blk_read,
    rodata_blk_write,
    rodata_blk_control,
};
#endif

int aic_nand_rodata_init(void)
{
    struct rt_device *mtd_dev;
    struct rt_mtd_nand_device *mtd;
    rt_uint32_t pages_per_block;
    rt_err_t ret;

    if (rodata.ready)
        return RT_EOK;

    mtd_dev = rt_device_find(RODATA_DEV_NAME);
    if (mtd_dev == RT_NULL)
    {
        rt_kprintf("rodata: MTD '%s' not found\n", RODATA_DEV_NAME);
        return -RT_EIO;
    }
    if (mtd_dev->type != RT_Device_Class_MTD)
    {
        rt_kprintf("rodata: '%s' is not an MTD device\n", RODATA_DEV_NAME);
        return -RT_EINVAL;
    }

    mtd = RT_MTD_NAND_DEVICE(mtd_dev);
    pages_per_block = mtd->pages_per_block;
    if (mtd->page_size == 0 || pages_per_block == 0 ||
        (mtd->page_size % RODATA_SECTOR_SIZE) != 0)
    {
        rt_kprintf("rodata: unsupported geometry page=%u ppb=%u\n",
                   mtd->page_size, pages_per_block);
        return -RT_EINVAL;
    }

    rodata.page_buf = rt_malloc(mtd->page_size);
    if (rodata.page_buf == RT_NULL)
        return -RT_ENOMEM;

    ret = rt_mutex_init(&rodata.lock, "nandro", RT_IPC_FLAG_PRIO);
    if (ret != RT_EOK)
    {
        rt_free(rodata.page_buf);
        rodata.page_buf = RT_NULL;
        return ret;
    }

    rodata.mtd = mtd;
    rodata.mtd_dev = mtd_dev;
    rodata.cached_page = 0xFFFFFFFFU;
    rodata.block_size = pages_per_block * mtd->page_size;
    rodata.sector_count = (rt_uint32_t)mtd->block_total *
                          rodata.block_size / RODATA_SECTOR_SIZE;

    rodata.parent.type = RT_Device_Class_Block;
    rodata.parent.rx_indicate = RT_NULL;
    rodata.parent.tx_complete = RT_NULL;
#ifdef RT_USING_DEVICE_OPS
    rodata.parent.ops = &rodata_blk_ops;
#else
    rodata.parent.init = RT_NULL;
    rodata.parent.open = rodata_blk_open;
    rodata.parent.close = rodata_blk_close;
    rodata.parent.read = rodata_blk_read;
    rodata.parent.write = rodata_blk_write;
    rodata.parent.control = rodata_blk_control;
#endif

    ret = rt_device_register(&rodata.parent, RODATA_BLK_NAME,
                             RT_DEVICE_FLAG_RDONLY);
    if (ret != RT_EOK)
    {
        rt_mutex_detach(&rodata.lock);
        rt_free(rodata.page_buf);
        rodata.page_buf = RT_NULL;
        return ret;
    }

    rodata.ready = RT_TRUE;
    rt_kprintf("rodata: %s = %u KiB, %u sectors of %u bytes\n",
               RODATA_BLK_NAME, rodata.sector_count / 2,
               rodata.sector_count, RODATA_SECTOR_SIZE);
    return RT_EOK;
}

int aic_nand_rodata_mount(void)
{
    if (!rodata.ready)
        return -RT_EIO;

    if (dfs_mount(RODATA_BLK_NAME, aic_board_storage.rodata_mount, "elm", 0, RT_NULL) != 0)
    {
        rt_kprintf("rodata: mount failed (image not FAT or ECC error)\n");
        return -RT_ERROR;
    }

    rt_kprintf("rodata: mounted at /rodata (read-only)\n");
    return RT_EOK;
}
