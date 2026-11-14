/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Storage stack regression test: read-only FAT on the rodata partition
 * and LittleFS plus the persistent bad-block table on nand_scratch.
 *
 * The rodata partition is never written: the test checks the FAT boot
 * sector through the read-only block device, confirms that a write is
 * rejected, and walks the mounted FAT root.
 *
 * The LittleFS case creates, reads back, renames and removes files under
 * /nand.  It also keeps a small marker file so the test can be run
 * again after a reset: the printed boot number must increase, which
 * proves that both the filesystem and the bad-block mapping survived
 * the reboot.
 *
 * This file is compiled only in builds that enable the storage options;
 * other builds get an empty translation unit so the utest case count is
 * unchanged.
 */

#include <rtthread.h>

#include "utest.h"

#if defined(BSP_SPINAND_RODATA_FS) && defined(BSP_SPINAND_LITTLEFS)

#include <rtdevice.h>
#include <dfs_fs.h>
#include <dfs_file.h>
#include <string.h>

#include "drv_nand_lfs.h"

#define TC_FS_MAGIC         "AICP"
#define TC_FS_MARKER        "/nand/persist.bin"
#define TC_FS_DIR           "/nand/utest"
#define TC_FS_FILE          "/nand/utest/data.bin"
#define TC_FS_RENAMED       "/nand/utest/data2.bin"
#define TC_FS_DATA_SIZE     4096U

struct tc_fs_marker
{
    rt_uint8_t magic[4];
    rt_uint32_t boot;
};

static int fs_read_file(const char *path, void *data, rt_size_t size)
{
    struct dfs_file fd;
    int ret;

    fd_init(&fd);
    ret = dfs_file_open(&fd, path, O_RDONLY);
    if (ret < 0)
        return ret;

    ret = (int)dfs_file_read(&fd, data, size);
    dfs_file_close(&fd);
    return ret;
}

static int fs_write_file(const char *path, const void *data, rt_size_t size)
{
    struct dfs_file fd;
    int ret;

    fd_init(&fd);
    ret = dfs_file_open(&fd, path, O_WRONLY | O_CREAT | O_TRUNC);
    if (ret < 0)
        return ret;

    ret = (int)dfs_file_write(&fd, data, size);
    dfs_file_close(&fd);
    return (ret == (int)size) ? 0 : -EIO;
}

static rt_bool_t fs_exists(const char *path)
{
    struct stat st;

    return dfs_file_stat(path, &st) == 0;
}

static int fs_mkdir(const char *path)
{
    struct dfs_file fd;
    int ret;

    fd_init(&fd);
    ret = dfs_file_open(&fd, path, O_RDONLY | O_DIRECTORY | O_CREAT);
    if (ret < 0)
        return ret;

    dfs_file_close(&fd);
    return 0;
}

static int fs_count_entries(const char *path)
{
    struct dfs_file fd;
    struct dirent dirent;
    int count = 0;
    int ret;

    fd_init(&fd);
    if (dfs_file_open(&fd, path, O_RDONLY | O_DIRECTORY) < 0)
        return -1;

    while ((ret = dfs_file_getdents(&fd, &dirent, sizeof(dirent))) > 0)
        count++;

    dfs_file_close(&fd);
    return count;
}

static void tc_fs_rodata(void)
{
    struct statfs st;
    struct rt_device *blk;
    struct rt_device_blk_geometry geometry;
    rt_uint8_t *sector;
    int ret;

    uassert_int_equal(dfs_statfs("/rodata", &st), 0);
    uassert_true(st.f_blocks > 0);
    uassert_int_equal(st.f_bsize, 512);

    blk = rt_device_find("nand_rodata");
    uassert_true(blk != RT_NULL);
    if (blk == RT_NULL)
        return;

    uassert_int_equal(rt_device_open(blk, RT_DEVICE_OFLAG_RDONLY), RT_EOK);
    rt_memset(&geometry, 0, sizeof(geometry));
    uassert_int_equal(rt_device_control(blk,
                       RT_DEVICE_CTRL_BLK_GETGEOME, &geometry), RT_EOK);
    uassert_int_equal(geometry.bytes_per_sector, 512);

    sector = rt_malloc(512);
    if (sector == RT_NULL)
    {
        uassert_true(RT_FALSE);
        rt_device_close(blk);
        return;
    }

    ret = (int)rt_device_read(blk, 0, sector, 1);
    uassert_int_equal(ret, 1);
    uassert_int_equal(sector[510], 0x55);
    uassert_int_equal(sector[511], 0xAA);
    uassert_true(rt_memcmp(sector + 54, "FAT", 3) == 0 ||
                 rt_memcmp(sector + 82, "FAT", 3) == 0);

    /* The block device must reject any write without touching the NAND. */
    uassert_int_equal((int)rt_device_write(blk, 0, sector, 1), 0);
    uassert_int_equal(rt_device_control(blk,
                       RT_DEVICE_CTRL_BLK_ERASE, RT_NULL), -RT_EPERM);

    rt_free(sector);
    rt_device_close(blk);

    /* The mounted FAT root must be readable; an empty volume is valid. */
    ret = fs_count_entries("/rodata");
    uassert_true(ret >= 0);
    rt_kprintf("tc_nand_fs: /rodata mounted, %d root entries\n", ret);
}

static void tc_fs_littlefs(void)
{
    struct aic_nand_lfs_info info;
    struct statfs st;
    struct stat statbuf;
    rt_uint8_t *write_buf;
    rt_uint8_t *read_buf;
    struct tc_fs_marker marker;
    rt_uint32_t boot = 1;
    rt_size_t i;
    int ret;

    uassert_int_equal(dfs_statfs("/nand", &st), 0);
    uassert_true(st.f_blocks >= 4);
    uassert_true(st.f_bsize >= 512);
    uassert_true(st.f_bfree <= st.f_blocks);

    uassert_int_equal(aic_nand_lfs_get_info(&info), RT_EOK);
    uassert_true(info.mounted);
    uassert_true(info.logical >= 4);
    uassert_int_equal((rt_uint32_t)info.logical + info.spare + info.bad + 2,
                      info.total);
    uassert_true(info.slot[0] != info.slot[1]);
    uassert_true(info.slot[0] < info.total);
    uassert_true(info.slot[1] < info.total);
    uassert_true(info.sequence > 0);

    rt_kprintf("tc_nand_fs: nand_scratch %u blocks, %u logical, "
               "%u spare, %u bad, BBT seq %u\n",
               info.total, info.logical, info.spare, info.bad,
               info.sequence);

    write_buf = rt_malloc(TC_FS_DATA_SIZE);
    read_buf = rt_malloc(TC_FS_DATA_SIZE);
    if (write_buf == RT_NULL || read_buf == RT_NULL)
    {
        uassert_true(RT_FALSE);
        rt_free(write_buf);
        rt_free(read_buf);
        return;
    }
    for (i = 0; i < TC_FS_DATA_SIZE; i++)
        write_buf[i] = (rt_uint8_t)(i * 7U + 3U);

    /* A stale file from an earlier run must not affect this check. */
    if (fs_exists(TC_FS_FILE))
        dfs_file_unlink(TC_FS_FILE);
    if (fs_exists(TC_FS_RENAMED))
        dfs_file_unlink(TC_FS_RENAMED);
    if (fs_exists(TC_FS_DIR))
        dfs_file_unlink(TC_FS_DIR);

    uassert_int_equal(fs_mkdir(TC_FS_DIR), 0);
    uassert_true(fs_exists(TC_FS_DIR));

    uassert_int_equal(fs_write_file(TC_FS_FILE, write_buf,
                                    TC_FS_DATA_SIZE), 0);
    uassert_true(fs_exists(TC_FS_FILE));

    rt_memset(read_buf, 0, TC_FS_DATA_SIZE);
    ret = fs_read_file(TC_FS_FILE, read_buf, TC_FS_DATA_SIZE);
    uassert_int_equal(ret, (int)TC_FS_DATA_SIZE);
    uassert_true(rt_memcmp(write_buf, read_buf, TC_FS_DATA_SIZE) == 0);

    rt_memset(&statbuf, 0, sizeof(statbuf));
    uassert_int_equal(dfs_file_stat(TC_FS_FILE, &statbuf), 0);
    uassert_int_equal(statbuf.st_size, TC_FS_DATA_SIZE);

    uassert_int_equal(dfs_file_rename(TC_FS_FILE, TC_FS_RENAMED), 0);
    uassert_false(fs_exists(TC_FS_FILE));
    uassert_true(fs_exists(TC_FS_RENAMED));

    uassert_int_equal(dfs_file_unlink(TC_FS_RENAMED), 0);
    uassert_false(fs_exists(TC_FS_RENAMED));

    /* The directory must be empty again and removable. */
    uassert_int_equal(fs_count_entries(TC_FS_DIR), 0);
    uassert_int_equal(dfs_file_unlink(TC_FS_DIR), 0);
    uassert_false(fs_exists(TC_FS_DIR));

    /*
     * Persistence marker: after a reset this file is still here and the
     * counter is incremented.  The HIL runs this suite twice with a
     * reboot in between and checks that the second output says boot=2.
     */
    rt_memset(&marker, 0, sizeof(marker));
    if (fs_exists(TC_FS_MARKER))
    {
        ret = fs_read_file(TC_FS_MARKER, &marker, sizeof(marker));
        if (ret == (int)sizeof(marker) &&
            rt_memcmp(marker.magic, TC_FS_MAGIC, 4) == 0)
            boot = marker.boot + 1;
        else
            rt_kprintf("tc_nand_fs: marker unreadable, restarting count\n");
    }

    rt_memcpy(marker.magic, TC_FS_MAGIC, 4);
    marker.boot = boot;
    uassert_int_equal(fs_write_file(TC_FS_MARKER, &marker,
                                    sizeof(marker)), 0);
    rt_kprintf("tc_nand_fs: persistence boot=%u\n", boot);

    rt_free(write_buf);
    rt_free(read_buf);
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_fs_rodata);
    UTEST_UNIT_RUN(tc_fs_littlefs);
}
UTEST_TC_EXPORT(testcase, "bsp.d213.spinand.fs", RT_NULL, RT_NULL, 180);

#endif /* BSP_SPINAND_RODATA_FS && BSP_SPINAND_LITTLEFS */
