/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Read-only A/B slot diagnostics for the ArtInChip Bootloader contract.
 *
 * The vendor image writes a redundant U-Boot environment:
 *
 *   env, env_r: 4096-byte image, u32 CRC, u8 flags, 4091 bytes of
 *               NUL-separated key=value data; CRC32 covers the data.
 *
 * The Bootloader selects the copy with the FLAG_INCREMENTAL rule and
 * uses osAB_next/osAB_now to choose between the 'os' and 'os_r' FIT
 * partitions.  This file only reads and reports those facts.  It does
 * not write env, os or os_r.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>

#include "drv_ota_slot.h"
#include "drv_spinand.h"

#define OTA_ENV_PART0           "env"
#define OTA_ENV_PART1           "env_r"
#define OTA_ENV_SIZE            4096U
#define OTA_ENV_HDR_SIZE        5U
#define OTA_ENV_DATA_SIZE       (OTA_ENV_SIZE - OTA_ENV_HDR_SIZE)

#define OTA_FIT_MAGIC           0xD00DFEEDU

struct ota_env_ctx
{
    rt_uint8_t *buf[AIC_OTA_ENV_COPIES];
    rt_uint8_t  flags[AIC_OTA_ENV_COPIES];
    rt_uint32_t crc[AIC_OTA_ENV_COPIES];
    rt_bool_t   read_ok[AIC_OTA_ENV_COPIES];
    rt_bool_t   valid[AIC_OTA_ENV_COPIES];
    rt_int8_t   active;
};

static rt_uint32_t ota_get_le32(const rt_uint8_t *p)
{
    return (rt_uint32_t)p[0] | ((rt_uint32_t)p[1] << 8) |
           ((rt_uint32_t)p[2] << 16) | ((rt_uint32_t)p[3] << 24);
}

static rt_uint32_t ota_crc32(const rt_uint8_t *data, rt_size_t len)
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

    return crc ^ 0xFFFFFFFFU;
}

/*
 * Read the env image from an MTD partition, skipping factory-bad
 * blocks exactly like the vendor env loader does.
 */
static int ota_env_read_part(const char *part, rt_uint8_t *buf, rt_size_t size)
{
    struct rt_mtd_nand_device *mtd;
    struct rt_device *dev;
    rt_uint32_t page, pages, done = 0;
    rt_err_t ret;

    dev = rt_device_find(part);
    if (dev == RT_NULL || dev->type != RT_Device_Class_MTD)
        return -RT_ENOENT;

    mtd = (struct rt_mtd_nand_device *)dev;
    if (mtd->page_size == 0 || mtd->pages_per_block == 0 ||
        (size % mtd->page_size) != 0)
        return -RT_EINVAL;

    pages = (rt_uint32_t)mtd->block_total * mtd->pages_per_block;
    for (page = 0; done < size; )
    {
        if (page >= pages)
            return -RT_EIO;

        if ((page % mtd->pages_per_block) == 0 &&
            rt_mtd_nand_check_block(mtd, page / mtd->pages_per_block) != RT_EOK)
        {
            page += mtd->pages_per_block;
            continue;
        }

        ret = rt_mtd_nand_read(mtd, page, buf + done, mtd->page_size,
                               RT_NULL, 0);
        if (ret != RT_MTD_EOK && ret != RT_MTD_EECC_CORRECT)
            return -RT_EIO;

        done += mtd->page_size;
        page++;
    }

    return RT_EOK;
}

static void ota_env_ctx_close(struct ota_env_ctx *ctx);

static int ota_env_ctx_open(struct ota_env_ctx *ctx)
{
    const char *parts[AIC_OTA_ENV_COPIES] = {OTA_ENV_PART0, OTA_ENV_PART1};
    int i;

    rt_memset(ctx, 0, sizeof(*ctx));
    ctx->active = -1;

    for (i = 0; i < AIC_OTA_ENV_COPIES; i++)
    {
        ctx->buf[i] = (rt_uint8_t *)rt_malloc(OTA_ENV_SIZE);
        if (ctx->buf[i] == RT_NULL)
            goto fail;

        if (ota_env_read_part(parts[i], ctx->buf[i], OTA_ENV_SIZE) != RT_EOK)
            continue;

        ctx->read_ok[i] = RT_TRUE;
        ctx->crc[i] = ota_get_le32(ctx->buf[i]);
        ctx->flags[i] = ctx->buf[i][4];
        ctx->valid[i] = (ota_crc32(ctx->buf[i] + OTA_ENV_HDR_SIZE,
                                   OTA_ENV_DATA_SIZE) == ctx->crc[i]);
    }

    if (!ctx->read_ok[0] && !ctx->read_ok[1])
    {
        ota_env_ctx_close(ctx);
        return -RT_EIO;
    }

    /* FLAG_INCREMENTAL selection, matching packages/artinchip/env/env.c. */
    if (ctx->valid[0] && !ctx->valid[1])
    {
        ctx->active = 0;
    }
    else if (!ctx->valid[0] && ctx->valid[1])
    {
        ctx->active = 1;
    }
    else if (!ctx->valid[0] && !ctx->valid[1])
    {
        ctx->active = -1;
    }
    else if (ctx->flags[0] == 255U && ctx->flags[1] == 0U)
    {
        ctx->active = 1;
    }
    else if ((ctx->flags[1] == 255U && ctx->flags[0] == 0U) ||
             ctx->flags[0] >= ctx->flags[1])
    {
        ctx->active = 0;
    }
    else
    {
        ctx->active = 1;
    }

    return RT_EOK;

fail:
    for (i = 0; i < AIC_OTA_ENV_COPIES; i++)
    {
        rt_free(ctx->buf[i]);
        ctx->buf[i] = RT_NULL;
    }
    return -RT_ENOMEM;
}

static void ota_env_ctx_close(struct ota_env_ctx *ctx)
{
    int i;

    for (i = 0; i < AIC_OTA_ENV_COPIES; i++)
    {
        rt_free(ctx->buf[i]);
        ctx->buf[i] = RT_NULL;
    }
}

static const char *ota_env_ctx_find(struct ota_env_ctx *ctx, const char *name)
{
    const char *data;
    const char *end;
    rt_size_t name_len;

    if (ctx->active < 0)
        return RT_NULL;

    data = (const char *)ctx->buf[ctx->active] + OTA_ENV_HDR_SIZE;
    end = data + OTA_ENV_DATA_SIZE;
    name_len = rt_strlen(name);

    while (data < end && *data != '\0')
    {
        const char *nul = (const char *)memchr(data, '\0', end - data);

        if (nul == RT_NULL)
            break;
        if (rt_strncmp(data, name, name_len) == 0 &&
            data[name_len] == '=')
            return data + name_len + 1;
        data = nul + 1;
    }

    return RT_NULL;
}

int aic_ota_env_status(struct aic_ota_env_status *status)
{
    struct ota_env_ctx ctx;
    int i, ret;

    if (status == RT_NULL)
        return -RT_EINVAL;

    ret = ota_env_ctx_open(&ctx);
    if (ret != RT_EOK)
        return ret;

    rt_memset(status, 0, sizeof(*status));
    for (i = 0; i < AIC_OTA_ENV_COPIES; i++)
    {
        status->read_ok[i] = ctx.read_ok[i];
        status->valid[i] = ctx.valid[i];
        status->flags[i] = ctx.flags[i];
        status->crc[i] = ctx.crc[i];
    }
    status->active = ctx.active;

    ota_env_ctx_close(&ctx);
    return RT_EOK;
}

int aic_ota_env_get(const char *name, char *value, rt_size_t size)
{
    struct ota_env_ctx ctx;
    const char *val;
    rt_size_t len;
    int ret;

    if (name == RT_NULL || value == RT_NULL || size == 0)
        return -RT_EINVAL;

    ret = ota_env_ctx_open(&ctx);
    if (ret != RT_EOK)
        return ret;

    if (ctx.active < 0)
    {
        ota_env_ctx_close(&ctx);
        return -RT_EIO;
    }

    val = ota_env_ctx_find(&ctx, name);
    if (val == RT_NULL)
    {
        ota_env_ctx_close(&ctx);
        return -RT_ENOENT;
    }

    len = rt_strlen(val);
    if (len >= size)
    {
        ota_env_ctx_close(&ctx);
        return -RT_EINVAL;
    }

    rt_memcpy(value, val, len + 1);
    ota_env_ctx_close(&ctx);
    return RT_EOK;
}

int aic_ota_env_dump(void)
{
    struct ota_env_ctx ctx;
    const char *data;
    const char *end;
    int ret;

    ret = ota_env_ctx_open(&ctx);
    if (ret != RT_EOK)
    {
        rt_kprintf("aic_ota_env_dump: env partitions unavailable (%d)\n", ret);
        return ret;
    }

    if (ctx.active < 0)
    {
        rt_kprintf("aic_ota_env_dump: no valid env copy\n");
        ota_env_ctx_close(&ctx);
        return -RT_EIO;
    }

    rt_kprintf("env dump (copy %s, flags 0x%02x, crc 0x%08x):\n",
               ctx.active == 0 ? "env" : "env_r",
               ctx.flags[ctx.active], ctx.crc[ctx.active]);

    data = (const char *)ctx.buf[ctx.active] + OTA_ENV_HDR_SIZE;
    end = data + OTA_ENV_DATA_SIZE;
    while (data < end && *data != '\0')
    {
        const char *nul = (const char *)memchr(data, '\0', end - data);

        if (nul == RT_NULL)
            break;
        rt_kprintf("  %s\n", data);
        data = nul + 1;
    }

    ota_env_ctx_close(&ctx);
    return RT_EOK;
}

static const char *ota_slot_name(const char *ab)
{
    if (ab == RT_NULL)
        return "?";
    if (rt_strcmp(ab, "A") == 0)
        return "os";
    if (rt_strcmp(ab, "B") == 0)
        return "os_r";
    return "?";
}

static void aic_ota_slot_status(int argc, char **argv)
{
    static const char *keys[] =
    {
        "osAB_now", "osAB_next",
        "rodataAB_next", "dataAB_next",
        "upgrade_available", "bootcount", "bootlimit",
    };
    struct aic_ota_env_status st;
    struct aic_nand_fit_info fit;
    char val[64];
    rt_bool_t dump = RT_FALSE;
    rt_size_t i;
    int ret;

    if (argc > 1)
    {
        if (rt_strcmp(argv[1], "env") != 0)
        {
            rt_kprintf("usage: aic_ota_slot_status [env]\n"
                       "  reads env/env_r and os/os_r; never writes them\n");
            return;
        }
        dump = RT_TRUE;
    }

    rt_kprintf("ArtInChip A/B slot status (read-only)\n");
    ret = aic_ota_env_status(&st);
    if (ret != RT_EOK)
    {
        rt_kprintf("  env     : unavailable (%d)\n", ret);
    }
    else
    {
        for (i = 0; i < AIC_OTA_ENV_COPIES; i++)
        {
            rt_kprintf("  %-7s : read=%s valid=%s flags=0x%02x crc=0x%08x\n",
                       i == 0 ? "env" : "env_r",
                       st.read_ok[i] ? "yes" : "no",
                       st.valid[i] ? "yes" : "no",
                       st.flags[i], st.crc[i]);
        }
        rt_kprintf("  active  : %s\n",
                   st.active < 0 ? "none" :
                   (st.active == 0 ? "env" : "env_r"));
    }

    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
    {
        if (aic_ota_env_get(keys[i], val, sizeof(val)) == RT_EOK)
            rt_kprintf("  %-17s: %s\n", keys[i], val);
        else
            rt_kprintf("  %-17s: (missing)\n", keys[i]);
    }

    if (aic_nand_get_fit_info("os", &fit) == RT_EOK)
    {
        rt_kprintf("  FIT os  : %s\n",
                   fit.present ? "valid" : "no valid FIT header");
    }
    else
    {
        rt_kprintf("  FIT os  : partition not registered\n");
    }

    if (aic_nand_get_fit_info("os_r", &fit) == RT_EOK)
    {
        rt_kprintf("  FIT os_r: %s\n",
                   fit.present ? "valid" : "no valid FIT header");
    }
    else
    {
        rt_kprintf("  FIT os_r: partition not registered\n");
    }

    if (aic_ota_env_get("osAB_next", val, sizeof(val)) == RT_EOK)
    {
        const char *part = ota_slot_name(val);

        if (strcmp(part, "?") != 0 &&
            aic_nand_get_fit_info(part, &fit) == RT_EOK && !fit.present)
        {
            rt_kprintf("  warning : osAB_next=%s selects '%s', which has no "
                       "valid FIT image\n", val, part);
        }
    }

    if (dump)
        aic_ota_env_dump();
}
MSH_CMD_EXPORT_ALIAS(aic_ota_slot_status, aic_ota_slot_status,
                     report Bootloader A/B env and os/os_r FIT slots read-only);
