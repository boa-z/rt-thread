/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * D213 storage stack glue.
 *
 * DFS v1 requires the mount point directory to exist before dfs_mount()
 * can place a filesystem on it.  The BSP does not ship a root filesystem,
 * so when a storage filesystem is enabled this driver installs a small
 * read-only romfs as "/" that only carries the mount points:
 *
 *   /rodata   read-only FAT image stored in the 'rodata' NAND partition
 *   /nand     LittleFS on the writable 'nand_scratch' tail
 *   /dev      reserved for devfs (mounted earlier by dfs_init)
 *
 * The romfs is a mount-point carrier, not a general purpose root
 * filesystem: creating files under it is refused by romfs.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <dfs_fs.h>
#include <dfs_romfs.h>

#ifdef BSP_SPINAND_RODATA_FS
int aic_nand_rodata_init(void);
int aic_nand_rodata_mount(void);
#endif

#ifdef BSP_SPINAND_LITTLEFS
int aic_nand_lfs_mount(void);
#endif

#if defined(BSP_SPINAND_RODATA_FS) || defined(BSP_SPINAND_LITTLEFS)

/* Empty directories; romfs uses size 0 and data NULL for an empty dir. */
static const struct romfs_dirent _storage_root[] =
{
    {ROMFS_DIRENT_DIR, "dev", (const rt_uint8_t *)RT_NULL, 0},
    {ROMFS_DIRENT_DIR, "rodata", (const rt_uint8_t *)RT_NULL, 0},
    {ROMFS_DIRENT_DIR, "nand", (const rt_uint8_t *)RT_NULL, 0},
};

/*
 * With RT_USING_DFS_ROMFS_USER_ROOT the component does not define
 * romfs_root; the board provides it.  The type is fixed by romfs.
 */
const struct romfs_dirent romfs_root =
{
    ROMFS_DIRENT_DIR, "/", (const rt_uint8_t *)_storage_root,
    sizeof(_storage_root) / sizeof(_storage_root[0])
};

static int storage_init(void)
{
    if (dfs_mount(RT_NULL, "/", "rom", 0, &romfs_root) != 0)
    {
        rt_kprintf("storage: root romfs mount failed, mount points "
                   "unavailable\n");
        return 0;
    }
    rt_kprintf("storage: root romfs mounted\n");

#ifdef BSP_SPINAND_RODATA_FS
    if (aic_nand_rodata_init() != RT_EOK)
        rt_kprintf("storage: nand_rodata device not available\n");
    else if (aic_nand_rodata_mount() != RT_EOK)
        rt_kprintf("storage: /rodata mount failed\n");
#endif

#ifdef BSP_SPINAND_LITTLEFS
    if (aic_nand_lfs_mount() != RT_EOK)
        rt_kprintf("storage: /nand mount failed\n");
#endif

    return 0;
}
INIT_FS_EXPORT(storage_init);

#endif /* BSP_SPINAND_RODATA_FS || BSP_SPINAND_LITTLEFS */
