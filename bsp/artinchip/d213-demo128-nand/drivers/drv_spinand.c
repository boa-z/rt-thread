/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D21x SPI NAND read-only driver.
 *
 * Command sequence, geometry and ECC decoding are ported from the
 * Luban-Lite SPI NAND stack (Apache-2.0):
 *   bsp/peripheral/spinand/spinand.c
 *   bsp/peripheral/spinand/winbond.c
 *   bsp/peripheral/spinand/inc/spinand.h
 *   bsp/artinchip/drv/spinand/spinand_port.c
 *
 * Deliberate differences from the vendor stack:
 *   - only read_id / read_page / check_block are implemented;
 *   - write_page / move_page / erase_block / mark_badblock are rejected
 *     before any NAND command is issued;
 *   - the vendor 13-entry MTD ops table (continuous_read, block status,
 *     OOB map, bitflip callback) is not ported into the upstream public
 *     MTD structure;
 *   - partitions come from pack/image_cfg.json at build time;
 *   - NFTL partitions are not exposed as linear MTD devices.
 *
 * The only NAND state changed at run time is the volatile configuration
 * register (buffer-read + on-chip ECC enable).  No program, erase,
 * bad-block marking or refresh command is ever generated.
 */

#include <rthw.h>
#include <rtthread.h>
#include <rtdevice.h>
#include <stdlib.h>
#include <string.h>

#include "drv_spinand.h"
#include "drv_qspi.h"
#include "spinand_parts_gen.h"

/* ------------------------------------------------------------------ */
/* NAND commands and registers (spinand.h)                            */
/* ------------------------------------------------------------------ */

#define SPINAND_CMD_RESET           0xFFU
#define SPINAND_CMD_GET_FEATURE     0x0FU
#define SPINAND_CMD_SET_FEATURE     0x1FU
#define SPINAND_CMD_PAGE_READ       0x13U
#define SPINAND_CMD_READ_CACHE      0x03U
#define SPINAND_CMD_READ_CACHE_X2   0x3BU
#define SPINAND_CMD_READ_CACHE_X4   0x6BU
#define SPINAND_CMD_READ_ID         0x9FU

#define SPINAND_REG_BLOCK_LOCK      0xA0U
#define SPINAND_REG_CFG             0xB0U
#define SPINAND_REG_STATUS          0xC0U

#define SPINAND_CFG_QUAD_ENABLE     (1U << 0)
#define SPINAND_CFG_BUF_ENABLE      (1U << 3)
#define SPINAND_CFG_ECC_ENABLE      (1U << 4)

#define SPINAND_STATUS_BUSY         (1U << 0)
#define SPINAND_STATUS_ECC_MASK     (0x3U << 4)

#define SPINAND_BUSY_TIMEOUT_US     30000U

#define SPINAND_PART_PRINT_MAX      4096U
#define SPINAND_FIT_MAGIC           0xD00DFEEDU

struct spi_nand_cmd_cfg
{
    rt_uint8_t opcode;
    rt_uint8_t opcode_bits;
    rt_uint8_t addr_bytes;
    rt_uint8_t addr_bits;
    rt_uint8_t dummy_bytes;
    rt_uint8_t data_bits;
};

static const struct spi_nand_cmd_cfg cmd_get_feature = {SPINAND_CMD_GET_FEATURE, 1, 1, 1, 0, 1};
static const struct spi_nand_cmd_cfg cmd_set_feature = {SPINAND_CMD_SET_FEATURE, 1, 1, 1, 0, 1};
static const struct spi_nand_cmd_cfg cmd_page_read   = {SPINAND_CMD_PAGE_READ, 1, 3, 1, 0, 0};
static const struct spi_nand_cmd_cfg cmd_read_id     = {SPINAND_CMD_READ_ID, 1, 0, 1, 1, 1};
static const struct spi_nand_cmd_cfg cmd_reset       = {SPINAND_CMD_RESET, 1, 0, 0, 0, 0};
static const struct spi_nand_cmd_cfg cmd_read_cache  = {SPINAND_CMD_READ_CACHE, 1, 2, 1, 1, 1};
static const struct spi_nand_cmd_cfg cmd_read_cache_x2 = {SPINAND_CMD_READ_CACHE_X2, 1, 2, 1, 1, 2};
static const struct spi_nand_cmd_cfg cmd_read_cache_x4 = {SPINAND_CMD_READ_CACHE_X4, 1, 2, 1, 1, 4};

/* ------------------------------------------------------------------ */
/* Supported chips (subset of Luban-Lite winbond.c)                   */
/* ------------------------------------------------------------------ */

struct spinand_model
{
    rt_uint8_t id[3];              /* manufacturer, device id byte 1/2 */
    const char *name;
    rt_uint16_t page_size;
    rt_uint16_t oob_size;
    rt_uint16_t pages_per_block;
    rt_uint16_t block_count;
    rt_uint16_t ecc_strength;
};

static const struct spinand_model spinand_models[] =
{
    {{0xEF, 0xAA, 0x21}, "W25N01GV", 2048,  64, 64, 1024, 1},
    {{0xEF, 0xAE, 0x21}, "W25N01KV", 2048,  64, 64, 1024, 4},
    {{0xEF, 0xAA, 0x22}, "W25N02KV", 2048, 128, 64, 2048, 1},
    {{0xEF, 0xBA, 0x22}, "W25N02KW", 2048, 128, 64, 1024, 1},
    {{0xEF, 0xBF, 0x22}, "W25N02JW", 2048,  64, 64, 2048, 1},
};

/* ------------------------------------------------------------------ */
/* Driver state                                                       */
/* ------------------------------------------------------------------ */

struct aic_spinand_state
{
    struct rt_qspi_device *qspi;
    struct rt_mutex lock;
    rt_bool_t lock_ready;
    rt_bool_t ready;
    rt_bool_t geometry_matches;
    const struct spinand_model *model;
    rt_uint8_t id[4];
    rt_uint16_t page_size;
    rt_uint16_t oob_size;
    rt_uint16_t pages_per_block;
    rt_uint16_t block_count;
    rt_uint16_t ecc_strength;
    rt_uint8_t qspi_dl_width;
    rt_uint8_t *pagebuf;
    rt_uint32_t pagebuf_len;
};

static struct aic_spinand_state spinand;

static rt_uint32_t nand_total_pages(void)
{
    return (rt_uint32_t)spinand.block_count * spinand.pages_per_block;
}

static int nand_lock(void)
{
    if (!spinand.lock_ready)
        return -RT_EIO;
    return (rt_mutex_take(&spinand.lock, RT_WAITING_FOREVER) == RT_EOK)
               ? RT_EOK : -RT_EIO;
}

static void nand_unlock(void)
{
    rt_mutex_release(&spinand.lock);
}

/* ------------------------------------------------------------------ */
/* QSPI message helper (spinand_port.c: qspi_messages_init)          */
/* ------------------------------------------------------------------ */

static int spinand_transfer(const struct spi_nand_cmd_cfg *cfg, rt_uint32_t addr,
                            const rt_uint8_t *sendbuf, rt_uint8_t *recvbuf,
                            rt_uint32_t datacount)
{
    struct rt_qspi_message msg;
    rt_ssize_t ret;

    if (spinand.qspi == RT_NULL)
        return -RT_EIO;

    rt_memset(&msg, 0, sizeof(msg));
    msg.instruction.content = cfg->opcode;
    msg.instruction.qspi_lines = cfg->opcode_bits;
    msg.address.content = addr;
    msg.address.size = cfg->addr_bytes;
    msg.address.qspi_lines = cfg->addr_bits;
    if (cfg->addr_bits != 0)
        msg.dummy_cycles = (rt_uint32_t)(cfg->dummy_bytes * 8U) / cfg->addr_bits;
    else
        msg.dummy_cycles = (rt_uint32_t)(cfg->dummy_bytes * 8U) /
                           (cfg->opcode_bits != 0 ? cfg->opcode_bits : 1U);
    msg.qspi_data_lines = cfg->data_bits;
    msg.parent.cs_take = 1;
    msg.parent.cs_release = 1;
    msg.parent.send_buf = sendbuf;
    msg.parent.recv_buf = recvbuf;
    msg.parent.length = datacount;
    msg.parent.next = RT_NULL;

    ret = rt_qspi_transfer_message(spinand.qspi, &msg);
    if (ret <= 0)
        return -RT_EIO;
    if (datacount != 0 && ret < (rt_ssize_t)datacount)
        return -RT_EIO;
    return RT_EOK;
}

static int spinand_read_reg(rt_uint8_t reg, rt_uint8_t *val)
{
    return spinand_transfer(&cmd_get_feature, reg, RT_NULL, val, 1);
}

static int spinand_write_reg(rt_uint8_t reg, rt_uint8_t val)
{
    return spinand_transfer(&cmd_set_feature, reg, &val, RT_NULL, 1);
}

static int spinand_read_status_reg(rt_uint8_t *status)
{
    return spinand_read_reg(SPINAND_REG_STATUS, status);
}

static int spinand_wait_busy(rt_uint8_t *status)
{
    rt_uint32_t waited = 0;
    rt_uint8_t sr = 0;

    for (;;)
    {
        if (spinand_read_status_reg(&sr) != RT_EOK)
            return -RT_EIO;
        if (!(sr & SPINAND_STATUS_BUSY))
            break;
        if (waited++ >= SPINAND_BUSY_TIMEOUT_US)
            return -RT_ETIMEOUT;
        rt_hw_us_delay(1);
    }
    if (status != RT_NULL)
        *status = sr;
    return RT_EOK;
}

static int spinand_config_update(rt_uint8_t mask, rt_uint8_t value)
{
    rt_uint8_t cfg;

    if (spinand_read_reg(SPINAND_REG_CFG, &cfg) != RT_EOK)
        return -RT_EIO;
    cfg = (rt_uint8_t)((cfg & ~mask) | (value & mask));
    return spinand_write_reg(SPINAND_REG_CFG, cfg);
}

static int spinand_read_id_op(rt_uint8_t id[4])
{
    return spinand_transfer(&cmd_read_id, 0, RT_NULL, id, 4);
}

static int spinand_load_page(rt_uint32_t page)
{
    return spinand_transfer(&cmd_page_read, page, RT_NULL, RT_NULL, 0);
}

static int spinand_read_cache(rt_uint32_t column, rt_uint8_t *buf, rt_uint32_t len)
{
    const struct spi_nand_cmd_cfg *cfg;

    if (spinand.qspi_dl_width == 4)
        cfg = &cmd_read_cache_x4;
    else if (spinand.qspi_dl_width == 2)
        cfg = &cmd_read_cache_x2;
    else
        cfg = &cmd_read_cache;
    return spinand_transfer(cfg, column, RT_NULL, buf, len);
}

/*
 * Decode the ECC bits of the status register.
 *   0  : no bit flips
 *   1  : bit flips corrected by the on-chip ECC
 *  -1  : uncorrectable
 *  -2  : reserved/invalid pattern
 */
static int spinand_decode_ecc(rt_uint8_t status)
{
    rt_uint8_t ecc = (status & SPINAND_STATUS_ECC_MASK) >> 4;

    switch (ecc)
    {
    case 0:
        return 0;
    case 1:
        return 1;
    case 2:
        return -1;
    default:
        return -2;
    }
}

/* ------------------------------------------------------------------ */
/* Raw page read                                                      */
/* ------------------------------------------------------------------ */

/* Caller must hold the flash lock. */
static int spinand_read_page_locked(rt_uint32_t page,
                                    rt_uint8_t *data, rt_uint32_t data_len,
                                    rt_uint8_t *oob, rt_uint32_t oob_len,
                                    int *result)
{
    rt_uint8_t status = 0;
    rt_uint32_t column;
    rt_uint32_t nbytes;
    int ecc;

    if (result != RT_NULL)
        *result = SPINAND_RO_EIO;

    if (page >= nand_total_pages())
        return SPINAND_RO_EINVAL;
    if (data_len > spinand.page_size || oob_len > spinand.oob_size)
        return SPINAND_RO_EINVAL;
    if ((data == RT_NULL || data_len == 0) && (oob == RT_NULL || oob_len == 0))
        return SPINAND_RO_EINVAL;
    if (spinand.pagebuf == RT_NULL)
        return SPINAND_RO_EIO;

    if (spinand_load_page(page) != RT_EOK)
        return SPINAND_RO_EIO;

    {
        int ret = spinand_wait_busy(&status);
        if (ret == -RT_ETIMEOUT)
            return SPINAND_RO_ETIMEOUT;
        if (ret != RT_EOK)
            return SPINAND_RO_EIO;
    }

    ecc = spinand_decode_ecc(status);
    if (ecc == -1)
    {
        if (result != RT_NULL)
            *result = SPINAND_RO_EECC;
        return SPINAND_RO_EECC;
    }
    if (ecc == -2)
    {
        rt_kprintf("spinand: undefined ECC status 0x%02x\n", status);
        return SPINAND_RO_EIO;
    }

    if (data != RT_NULL && data_len != 0)
    {
        column = 0;
        nbytes = spinand.page_size;
        if (oob != RT_NULL && oob_len != 0)
            nbytes += spinand.oob_size;
    }
    else
    {
        column = spinand.page_size;
        nbytes = spinand.oob_size;
    }

    if (nbytes > spinand.pagebuf_len)
        return SPINAND_RO_EIO;

    if (spinand_read_cache(column, spinand.pagebuf, nbytes) != RT_EOK)
        return SPINAND_RO_EIO;

    if (data != RT_NULL && data_len != 0)
        rt_memcpy(data, spinand.pagebuf, data_len);
    if (oob != RT_NULL && oob_len != 0)
    {
        rt_uint32_t oob_offset = (data != RT_NULL && data_len != 0)
                                     ? spinand.page_size : 0;
        rt_memcpy(oob, spinand.pagebuf + oob_offset, oob_len);
    }

    if (result != RT_NULL)
        *result = (ecc == 1) ? SPINAND_RO_ECC_OK : SPINAND_RO_OK;
    return (ecc == 1) ? SPINAND_RO_ECC_OK : SPINAND_RO_OK;
}

/* ------------------------------------------------------------------ */
/* Identification / initialisation                                    */
/* ------------------------------------------------------------------ */

static const struct spinand_model *spinand_match_model(const rt_uint8_t id[4])
{
    rt_uint32_t i;

    for (i = 0; i < sizeof(spinand_models) / sizeof(spinand_models[0]); i++)
    {
        if (id[0] == spinand_models[i].id[0] &&
            id[1] == spinand_models[i].id[1] &&
            id[2] == spinand_models[i].id[2])
            return &spinand_models[i];
    }
    return RT_NULL;
}

static int spinand_attach_qspi(void)
{
    struct rt_qspi_configuration cfg;
    struct rt_qspi_device *dev;
    rt_err_t ret;

    spinand.qspi = RT_NULL;

    /* Guarantee bus registration order even if linker order changes. */
    ret = rt_hw_qspi0_init();
    if (ret != RT_EOK)
    {
        rt_kprintf("spinand: qspi0 init failed (%d)\n", ret);
        return -RT_ERROR;
    }

    dev = (struct rt_qspi_device *)rt_malloc(sizeof(*dev));
    if (dev == RT_NULL)
    {
        rt_kprintf("spinand: no memory for rt_qspi_device\n");
        return -RT_ENOMEM;
    }
    rt_memset(dev, 0, sizeof(*dev));

    ret = rt_spi_bus_attach_device(&dev->parent, "spinand0", "qspi0", RT_NULL);
    if (ret != RT_EOK)
    {
        rt_kprintf("spinand: attach to qspi0 failed (%d)\n", ret);
        rt_free(dev);
        return -RT_ERROR;
    }

    rt_memset(&cfg, 0, sizeof(cfg));
    cfg.parent.mode = RT_SPI_MODE_0 | RT_SPI_MSB;
    cfg.parent.data_width = 8;
    cfg.parent.max_hz = BSP_QSPI0_FREQ;
    cfg.medium_size = 128UL * 1024UL * 1024UL;
    cfg.ddr_mode = 0;
    cfg.qspi_dl_width = 1;   /* stage 1: single-line data phase */

    ret = rt_qspi_configure(dev, &cfg);
    if (ret != RT_EOK)
    {
        rt_kprintf("spinand: qspi configure failed (%d)\n", ret);
        rt_spi_bus_detach_device(&dev->parent);
        rt_free(dev);
        return -RT_ERROR;
    }

    spinand.qspi = dev;
    spinand.qspi_dl_width = dev->config.qspi_dl_width;
    return RT_EOK;
}

static rt_err_t spinand_probe(void)
{
    rt_uint8_t id[4] = {0};
    const struct spinand_model *model;
    rt_uint32_t alloc;

    if (spinand_read_id_op(id) != RT_EOK)
    {
        rt_kprintf("spinand: JEDEC ID read failed\n");
        return -RT_EIO;
    }
    rt_memcpy(spinand.id, id, sizeof(id));
    rt_kprintf("spinand: JEDEC ID %02X %02X %02X %02X\n",
               id[0], id[1], id[2], id[3]);

    model = spinand_match_model(id);
    if (model == RT_NULL)
    {
        rt_kprintf("spinand: unsupported chip (no geometry table entry)\n");
        return -RT_ENOSYS;
    }

    spinand.model = model;
    spinand.page_size = model->page_size;
    spinand.oob_size = model->oob_size;
    spinand.pages_per_block = model->pages_per_block;
    spinand.block_count = model->block_count;
    spinand.ecc_strength = model->ecc_strength;
    rt_kprintf("spinand: %s, %u+%u B, %u pages/block, %u blocks\n",
               model->name, model->page_size, model->oob_size,
               model->pages_per_block, model->block_count);

    /* Only volatile configuration registers are touched.  No program,
     * erase, block lock or bad-block marker change is performed. */
    if (spinand_config_update(SPINAND_CFG_BUF_ENABLE,
                              SPINAND_CFG_BUF_ENABLE) != RT_EOK)
    {
        rt_kprintf("spinand: failed to enable buffer read mode\n");
        return -RT_EIO;
    }
    if (spinand_config_update(SPINAND_CFG_ECC_ENABLE,
                              SPINAND_CFG_ECC_ENABLE) != RT_EOK)
    {
        rt_kprintf("spinand: failed to enable on-chip ECC\n");
        return -RT_EIO;
    }
    {
        rt_uint8_t cfg = 0, sr = 0;
        if (spinand_read_reg(SPINAND_REG_CFG, &cfg) == RT_EOK &&
            spinand_read_status_reg(&sr) == RT_EOK)
            rt_kprintf("spinand: CFG=0x%02x STATUS=0x%02x\n", cfg, sr);
        if ((cfg & (SPINAND_CFG_BUF_ENABLE | SPINAND_CFG_ECC_ENABLE)) !=
            (SPINAND_CFG_BUF_ENABLE | SPINAND_CFG_ECC_ENABLE))
        {
            rt_kprintf("spinand: configuration did not stick (CFG=0x%02x)\n",
                       cfg);
            return -RT_EIO;
        }
    }

    alloc = (rt_uint32_t)spinand.page_size + spinand.oob_size;
    spinand.pagebuf = (rt_uint8_t *)rt_malloc(alloc);
    if (spinand.pagebuf == RT_NULL)
    {
        rt_kprintf("spinand: no memory for page buffer (%u bytes)\n", alloc);
        return -RT_ENOMEM;
    }
    spinand.pagebuf_len = alloc;

    spinand.geometry_matches =
        (spinand.page_size == SPINAND_CFG_PAGE_SIZE &&
         spinand.oob_size == SPINAND_CFG_OOB_SIZE &&
         spinand.pages_per_block ==
             (SPINAND_CFG_BLOCK_SIZE / SPINAND_CFG_PAGE_SIZE) &&
         spinand.block_count == SPINAND_CFG_TOTAL_BLOCKS);

    if (!spinand.geometry_matches)
    {
        rt_kprintf("spinand: WARNING detected geometry differs from "
                   "image_cfg.json (page %u/%u, oob %u/%u, ppb %u/%u, "
                   "blocks %u/%u)\n",
                   spinand.page_size, SPINAND_CFG_PAGE_SIZE,
                   spinand.oob_size, SPINAND_CFG_OOB_SIZE,
                   spinand.pages_per_block,
                   (SPINAND_CFG_BLOCK_SIZE / SPINAND_CFG_PAGE_SIZE),
                   spinand.block_count, SPINAND_CFG_TOTAL_BLOCKS);
    }

    spinand.ready = RT_TRUE;
    return RT_EOK;
}

/* ------------------------------------------------------------------ */
/* Standard RT-Thread MTD NAND ops                                    */
/* ------------------------------------------------------------------ */

static rt_err_t spinand_mtd_read_id(struct rt_mtd_nand_device *device)
{
    rt_uint8_t id[4];
    int ret;

    (void)device;
    if (nand_lock() != RT_EOK)
        return -RT_EIO;
    ret = spinand_read_id_op(id);
    nand_unlock();
    if (ret != RT_EOK)
        return -RT_EIO;
    rt_memcpy(spinand.id, id, sizeof(id));
    return RT_EOK;
}

static rt_err_t spinand_mtd_read_page(struct rt_mtd_nand_device *device,
                                      rt_off_t page,
                                      rt_uint8_t *data, rt_uint32_t data_len,
                                      rt_uint8_t *spare, rt_uint32_t spare_len)
{
    rt_uint32_t abs_page;
    int result = SPINAND_RO_EIO;
    int ret;

    if (device == RT_NULL || device->priv != &spinand)
        return -RT_EINVAL;
    if (page < 0 || device->pages_per_block == 0)
        return -RT_MTD_EIO;
    if ((rt_uint32_t)page / device->pages_per_block >= device->block_total)
        return -RT_MTD_EIO;

    abs_page = device->block_start * device->pages_per_block +
               (rt_uint32_t)page;

    if (nand_lock() != RT_EOK)
        return -RT_EIO;
    ret = spinand_read_page_locked(abs_page, data, data_len,
                                   spare, spare_len, &result);
    nand_unlock();

    switch (ret)
    {
    case SPINAND_RO_OK:
        return RT_MTD_EOK;
    case SPINAND_RO_ECC_OK:
        return RT_MTD_EECC_CORRECT;
    case SPINAND_RO_EECC:
        return RT_MTD_EECC;
    case SPINAND_RO_ETIMEOUT:
        return RT_MTD_EBUSY;
    case SPINAND_RO_EINVAL:
    default:
        return RT_MTD_EIO;
    }
}

static rt_err_t spinand_mtd_check_block(struct rt_mtd_nand_device *device,
                                        rt_uint32_t block)
{
    rt_uint32_t abs_block;
    rt_uint32_t page;
    rt_uint8_t marker = 0xFF;
    int result = SPINAND_RO_EIO;
    int ret;

    if (device == RT_NULL || device->priv != &spinand)
        return -RT_EINVAL;
    if (block >= device->block_total)
        return -RT_MTD_EIO;

    abs_block = device->block_start + block;
    page = abs_block * device->pages_per_block;

    if (nand_lock() != RT_EOK)
        return -RT_EIO;
    ret = spinand_read_page_locked(page, RT_NULL, 0, &marker, 1, &result);
    nand_unlock();

    if (ret == SPINAND_RO_EECC)
        return RT_MTD_EECC;
    if (ret != SPINAND_RO_OK && ret != SPINAND_RO_ECC_OK)
        return RT_MTD_EIO;

    /* Factory bad-block marker is OOB byte 0 (Winbond BBM). */
    return (marker != 0xFF) ? -RT_ERROR : RT_EOK;
}

/*
 * Read-only enforcement.  These stubs must exist: the upstream MTD core
 * uses RT_ASSERT() on write_page/move_page/erase_block, so a NULL pointer
 * would abort instead of returning an error.
 */
#if defined(BSP_SPINAND_READONLY)
static rt_err_t spinand_mtd_write_page(struct rt_mtd_nand_device *device,
                                       rt_off_t page,
                                       const rt_uint8_t *data, rt_uint32_t data_len,
                                       const rt_uint8_t *spare, rt_uint32_t spare_len)
{
    (void)device; (void)page; (void)data; (void)data_len;
    (void)spare; (void)spare_len;
    return -RT_EPERM;
}

static rt_err_t spinand_mtd_move_page(struct rt_mtd_nand_device *device,
                                      rt_off_t src_page, rt_off_t dst_page)
{
    (void)device; (void)src_page; (void)dst_page;
    return -RT_EPERM;
}

static rt_err_t spinand_mtd_erase_block(struct rt_mtd_nand_device *device,
                                        rt_uint32_t block)
{
    (void)device; (void)block;
    return -RT_EPERM;
}

static rt_err_t spinand_mtd_mark_badblock(struct rt_mtd_nand_device *device,
                                          rt_uint32_t block)
{
    (void)device; (void)block;
    return -RT_EPERM;
}
#else
#error "The D213 SPI NAND port currently supports read-only operation only"
#endif

static const struct rt_mtd_nand_driver_ops spinand_mtd_ops =
{
    .read_id = spinand_mtd_read_id,
    .read_page = spinand_mtd_read_page,
    .write_page = spinand_mtd_write_page,
    .move_page = spinand_mtd_move_page,
    .erase_block = spinand_mtd_erase_block,
    .check_block = spinand_mtd_check_block,
    .mark_badblock = spinand_mtd_mark_badblock,
};

#ifdef RT_USING_DEVICE_OPS
/*
 * The upstream MTD core's generic rt_device_read()/write() handlers simply
 * return the requested size without touching the flash.  That is unsafe for
 * a read-only port: a caller could mistake the size for a successful page
 * read.  Force callers onto the MTD API by failing both generic entry
 * points explicitly.  rt_mtd_nand_* bypasses these device ops.
 */
static rt_ssize_t spinand_ro_dev_read(rt_device_t dev, rt_off_t pos,
                                      void *buffer, rt_size_t size)
{
    (void)dev; (void)pos; (void)buffer; (void)size;
    return -RT_ENOSYS;
}

static rt_ssize_t spinand_ro_dev_write(rt_device_t dev, rt_off_t pos,
                                       const void *buffer, rt_size_t size)
{
    (void)dev; (void)pos; (void)buffer; (void)size;
    return -RT_EPERM;
}

static rt_err_t spinand_ro_dev_init(rt_device_t dev) { (void)dev; return RT_EOK; }
static rt_err_t spinand_ro_dev_open(rt_device_t dev, rt_uint16_t oflag)
{
    (void)dev; (void)oflag; return RT_EOK;
}
static rt_err_t spinand_ro_dev_close(rt_device_t dev) { (void)dev; return RT_EOK; }
static rt_err_t spinand_ro_dev_control(rt_device_t dev, int cmd, void *args)
{
    (void)dev; (void)cmd; (void)args; return RT_EOK;
}

static const struct rt_device_ops spinand_ro_device_ops =
{
    .init = spinand_ro_dev_init,
    .open = spinand_ro_dev_open,
    .close = spinand_ro_dev_close,
    .read = spinand_ro_dev_read,
    .write = spinand_ro_dev_write,
    .control = spinand_ro_dev_control,
};
#endif /* RT_USING_DEVICE_OPS */

/* ------------------------------------------------------------------ */
/* Partition registration                                             */
/* ------------------------------------------------------------------ */

static struct rt_mtd_nand_device spinand_mtd_parts[SPINAND_PART_COUNT];

static rt_err_t spinand_register_partitions(void)
{
    rt_uint32_t i;
    rt_uint32_t registered = 0;

    if (!spinand.geometry_matches)
    {
        rt_kprintf("spinand: partitions not registered, chip geometry "
                   "does not match image_cfg.json\n");
        return -RT_EIO;
    }

    for (i = 0; i < SPINAND_PART_COUNT; i++)
    {
        const struct spinand_part_desc *part = &spinand_parts[i];
        struct rt_mtd_nand_device *dev = &spinand_mtd_parts[i];
        rt_err_t ret;

        if (!part->linear)
            continue;   /* NFTL-backed region: raw page access only */

        if (part->start_block + part->block_count > spinand.block_count)
        {
            rt_kprintf("spinand: partition %s exceeds the chip\n", part->name);
            return -RT_EIO;
        }

        rt_memset(dev, 0, sizeof(*dev));
        dev->page_size = spinand.page_size;
        dev->oob_size = spinand.oob_size;
        dev->oob_free = 0;          /* raw OOB only; no user OOB mapping */
        dev->plane_num = 1;
        dev->pages_per_block = spinand.pages_per_block;
        dev->block_total = part->block_count;
        dev->block_start = part->start_block;
        dev->block_end = part->start_block + part->block_count - 1;
        dev->ops = &spinand_mtd_ops;
        dev->priv = &spinand;

        ret = rt_mtd_nand_register_device(part->name, dev);
        if (ret != RT_EOK)
        {
            rt_kprintf("spinand: register %s failed (%d)\n", part->name, ret);
            return ret;
        }
        /* The upstream helper always registers RDWR.  Reflect the real
         * capability in the device flag for list_device()/debugging.  The
         * flag is informational only; enforcement is in the ops stubs. */
        dev->parent.flag = RT_DEVICE_FLAG_RDONLY | RT_DEVICE_FLAG_STANDALONE;
#ifdef RT_USING_DEVICE_OPS
        /* Fail generic rt_device_read()/write(); use rt_mtd_nand_* instead. */
        dev->parent.ops = &spinand_ro_device_ops;
#endif
        registered++;
    }

    /* The upstream registration helper publishes RT_DEVICE_FLAG_RDWR
     * regardless of the ops table.  Do not rely on the flag: writes are
     * rejected by spinand_mtd_write_page()/erase/mark stubs above. */
    rt_kprintf("spinand: %u linear MTD partition(s) registered, read-only\n",
               registered);
    return RT_EOK;
}

/* ------------------------------------------------------------------ */
/* Public read-only helpers                                           */
/* ------------------------------------------------------------------ */

rt_err_t aic_spinand_ro_ready(void)
{
    return spinand.ready ? RT_EOK : -RT_ENOSYS;
}

rt_err_t aic_spinand_ro_get_info(struct aic_spinand_ro_info *info)
{
    if (info == RT_NULL)
        return -RT_EINVAL;
    if (!spinand.ready)
        return -RT_ENOSYS;

    rt_memset(info, 0, sizeof(*info));
    rt_memcpy(info->id, spinand.id, sizeof(info->id));
    info->model = (spinand.model != RT_NULL) ? spinand.model->name : "unknown";
    info->page_size = spinand.page_size;
    info->oob_size = spinand.oob_size;
    info->pages_per_block = spinand.pages_per_block;
    info->block_count = spinand.block_count;
    info->ecc_strength = spinand.ecc_strength;
    info->bus_hz = (spinand.qspi != RT_NULL) ? spinand.qspi->config.parent.max_hz : 0;
    return RT_EOK;
}

rt_err_t aic_spinand_ro_read_status(rt_uint8_t *status)
{
    int ret;

    if (status == RT_NULL)
        return -RT_EINVAL;
    if (!spinand.ready)
        return -RT_ENOSYS;
    if (nand_lock() != RT_EOK)
        return -RT_EIO;
    ret = spinand_read_status_reg(status);
    nand_unlock();
    return (ret == RT_EOK) ? RT_EOK : -RT_EIO;
}

rt_err_t aic_spinand_ro_read_cfg(rt_uint8_t *cfg)
{
    int ret;

    if (cfg == RT_NULL)
        return -RT_EINVAL;
    if (!spinand.ready)
        return -RT_ENOSYS;
    if (nand_lock() != RT_EOK)
        return -RT_EIO;
    ret = spinand_read_reg(SPINAND_REG_CFG, cfg);
    nand_unlock();
    return (ret == RT_EOK) ? RT_EOK : -RT_EIO;
}

int aic_spinand_ro_read_page(rt_uint32_t page,
                             rt_uint8_t *data, rt_uint32_t data_len,
                             rt_uint8_t *oob, rt_uint32_t oob_len,
                             int *result)
{
    int ret;

    if (result != RT_NULL)
        *result = SPINAND_RO_EIO;
    if (!spinand.ready)
        return SPINAND_RO_EIO;
    if (nand_lock() != RT_EOK)
        return SPINAND_RO_EIO;
    ret = spinand_read_page_locked(page, data, data_len, oob, oob_len, result);
    nand_unlock();
    return ret;
}

rt_err_t aic_spinand_ro_check_block(rt_uint32_t block, rt_bool_t *bad)
{
    rt_uint8_t marker = 0xFF;
    int result = SPINAND_RO_EIO;
    int ret;

    if (bad == RT_NULL)
        return -RT_EINVAL;
    if (!spinand.ready)
        return -RT_ENOSYS;
    if (block >= spinand.block_count)
        return -RT_EINVAL;

    if (nand_lock() != RT_EOK)
        return -RT_EIO;
    ret = spinand_read_page_locked(block * spinand.pages_per_block,
                                   RT_NULL, 0, &marker, 1, &result);
    nand_unlock();

    if (ret == SPINAND_RO_EECC)
        return -RT_EIO;
    if (ret != SPINAND_RO_OK && ret != SPINAND_RO_ECC_OK)
        return -RT_EIO;

    *bad = (marker != 0xFF) ? RT_TRUE : RT_FALSE;
    return RT_EOK;
}

/* ------------------------------------------------------------------ */
/* MSH commands (read-only)                                           */
/* ------------------------------------------------------------------ */

#ifdef RT_USING_FINSH

static const char *nand_ecc_text(int result)
{
    switch (result)
    {
    case SPINAND_RO_OK:
        return "clean";
    case SPINAND_RO_ECC_OK:
        return "corrected";
    case SPINAND_RO_EECC:
        return "UNCORRECTABLE";
    case SPINAND_RO_ETIMEOUT:
        return "timeout";
    case SPINAND_RO_EINVAL:
        return "invalid";
    default:
        return "transport error";
    }
}

static void nand_hex_dump(const char *tag, const rt_uint8_t *buf,
                          rt_uint32_t len, rt_uint32_t max)
{
    rt_uint32_t i;

    if (len > max)
        len = max;
    rt_kprintf("%s (%u byte%s shown):", tag, len, len == 1 ? "" : "s");
    for (i = 0; i < len; i++)
    {
        if (i % 16 == 0)
            rt_kprintf("\n  %04x:", i);
        rt_kprintf(" %02x", buf[i]);
    }
    rt_kprintf("\n");
}

static rt_uint32_t nand_crc32(const rt_uint8_t *buf, rt_uint32_t len)
{
    rt_uint32_t crc = 0xFFFFFFFFU;
    rt_uint32_t i, bit;

    for (i = 0; i < len; i++)
    {
        crc ^= buf[i];
        for (bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return crc ^ 0xFFFFFFFFU;
}

static int nand_parse_u32(const char *text, rt_uint32_t *value)
{
    char *end = RT_NULL;
    unsigned long val;

    if (text == RT_NULL || *text == '\0')
        return -1;
    val = strtoul(text, &end, 0);
    if (end == RT_NULL || *end != '\0')
        return -1;
    *value = (rt_uint32_t)val;
    return 0;
}

static void aic_nand_info(void)
{
    struct aic_spinand_ro_info info;

    if (aic_spinand_ro_get_info(&info) != RT_EOK)
    {
        rt_kprintf("aic_nand_info: SPI NAND is not initialised\n");
        return;
    }

    rt_kprintf("SPI NAND (read-only)\n");
    rt_kprintf("  device  : spinand0 on qspi0\n");
    rt_kprintf("  jedec   : %02X %02X %02X %02X\n",
               info.id[0], info.id[1], info.id[2], info.id[3]);
    rt_kprintf("  model   : %s\n", info.model);
    rt_kprintf("  page    : %u bytes\n", info.page_size);
    rt_kprintf("  oob     : %u bytes\n", info.oob_size);
    rt_kprintf("  block   : %u pages (%u bytes)\n", info.pages_per_block,
               (unsigned int)info.page_size * info.pages_per_block);
    rt_kprintf("  blocks  : %u (%u MiB)\n", info.block_count,
               (unsigned int)info.block_count * info.page_size *
               info.pages_per_block / (1024U * 1024U));
    rt_kprintf("  ecc     : on-chip, strength %u bit/512B\n",
               info.ecc_strength);
    rt_kprintf("  qspi    : %u Hz requested, 1-1-1 synchronous\n", info.bus_hz);
    rt_kprintf("  mode    : read-only (program/erase/mark rejected)\n");
}
MSH_CMD_EXPORT_ALIAS(aic_nand_info, aic_nand_info,
                     show SPI NAND identification and geometry);

static void aic_nand_status(void)
{
    rt_uint8_t sr = 0, cfg = 0;
    int ecc;

    rt_hw_qspi0_dump();
    if (!spinand.ready)
    {
        rt_kprintf("aic_nand_status: SPI NAND is not initialised\n");
        if (spinand.qspi != RT_NULL &&
            spinand_read_status_reg(&sr) == RT_EOK)
            rt_kprintf("aic_nand_status: raw STATUS=0x%02x\n", sr);
        return;
    }
    if (aic_spinand_ro_read_status(&sr) != RT_EOK)
    {
        rt_kprintf("aic_nand_status: failed to read status register\n");
        return;
    }
    if (aic_spinand_ro_read_cfg(&cfg) != RT_EOK)
    {
        rt_kprintf("aic_nand_status: failed to read config register\n");
        return;
    }

    ecc = spinand_decode_ecc(sr);
    rt_kprintf("status  : 0x%02X\n", sr);
    rt_kprintf("  busy        : %s\n", (sr & SPINAND_STATUS_BUSY) ? "yes" : "no");
    rt_kprintf("  erase fail  : %s\n", (sr & (1U << 2)) ? "yes" : "no");
    rt_kprintf("  program fail: %s\n", (sr & (1U << 3)) ? "yes" : "no");
    rt_kprintf("  ecc bits    : %u (%s)\n", (sr & SPINAND_STATUS_ECC_MASK) >> 4,
               (ecc == 0) ? "no bit flips" :
               (ecc == 1) ? "bit flips corrected" :
               (ecc == -1) ? "uncorrectable" : "reserved");
    rt_kprintf("config  : 0x%02X\n", cfg);
    rt_kprintf("  quad        : %s\n", (cfg & SPINAND_CFG_QUAD_ENABLE) ? "on" : "off");
    rt_kprintf("  buffer read : %s\n", (cfg & SPINAND_CFG_BUF_ENABLE) ? "on" : "off");
    rt_kprintf("  on-chip ecc : %s\n", (cfg & SPINAND_CFG_ECC_ENABLE) ? "on" : "off");
    rt_kprintf("note    : erase/program fail bits are latched by the chip\n"
               "          and are only meaningful after a write or erase,\n"
               "          which this driver never issues.\n");
}
MSH_CMD_EXPORT_ALIAS(aic_nand_status, aic_nand_status,
                     show SPI NAND status and config registers);

static void aic_nand_read_page(int argc, char **argv)
{
    struct aic_spinand_ro_info info;
    rt_uint8_t *data;
    rt_uint8_t *oob;
    rt_uint32_t page;
    int result = SPINAND_RO_EIO;
    int ret;

    if (argc != 2)
    {
        rt_kprintf("usage: aic_nand_read_page <physical_page>\n");
        return;
    }
    if (nand_parse_u32(argv[1], &page) != 0)
    {
        rt_kprintf("aic_nand_read_page: bad page number '%s'\n", argv[1]);
        return;
    }
    if (aic_spinand_ro_get_info(&info) != RT_EOK)
    {
        rt_kprintf("aic_nand_read_page: SPI NAND is not initialised\n");
        return;
    }
    if (page >= (rt_uint32_t)info.block_count * info.pages_per_block)
    {
        rt_kprintf("aic_nand_read_page: page %u out of range (max %u)\n",
                   page, (unsigned int)info.block_count * info.pages_per_block - 1U);
        return;
    }

    data = (rt_uint8_t *)rt_malloc(info.page_size);
    oob = (rt_uint8_t *)rt_malloc(info.oob_size);
    if (data == RT_NULL || oob == RT_NULL)
    {
        rt_kprintf("aic_nand_read_page: out of memory\n");
        rt_free(data);
        rt_free(oob);
        return;
    }

    ret = aic_spinand_ro_read_page(page, data, info.page_size,
                                   oob, info.oob_size, &result);
    rt_kprintf("physical page %u: %s\n", page, nand_ecc_text(ret));
    if (ret == SPINAND_RO_OK || ret == SPINAND_RO_ECC_OK)
    {
        nand_hex_dump("data", data, info.page_size, 32);
        nand_hex_dump("oob ", oob, info.oob_size, 16);
    }
    rt_free(data);
    rt_free(oob);
}
MSH_CMD_EXPORT_ALIAS(aic_nand_read_page, aic_nand_read_page,
                     read a raw physical NAND page (page 0..N-1));

static void aic_nand_check_block(int argc, char **argv)
{
    struct aic_spinand_ro_info info;
    rt_uint32_t block;
    rt_bool_t bad = RT_FALSE;
    rt_err_t ret;

    if (argc != 2)
    {
        rt_kprintf("usage: aic_nand_check_block <physical_block>\n");
        return;
    }
    if (nand_parse_u32(argv[1], &block) != 0)
    {
        rt_kprintf("aic_nand_check_block: bad block number '%s'\n", argv[1]);
        return;
    }
    if (aic_spinand_ro_get_info(&info) != RT_EOK)
    {
        rt_kprintf("aic_nand_check_block: SPI NAND is not initialised\n");
        return;
    }
    if (block >= info.block_count)
    {
        rt_kprintf("aic_nand_check_block: block %u out of range (max %u)\n",
                   block, info.block_count - 1U);
        return;
    }

    ret = aic_spinand_ro_check_block(block, &bad);
    if (ret != RT_EOK)
    {
        rt_kprintf("physical block %u: marker read failed (%d)\n", block, ret);
        return;
    }
    rt_kprintf("physical block %u: %s (OOB[0]=%s)\n", block,
               bad ? "FACTORY BAD" : "good",
               bad ? "not 0xFF" : "0xFF");
}
MSH_CMD_EXPORT_ALIAS(aic_nand_check_block, aic_nand_check_block,
                     check the factory bad-block marker of a physical block);

static void nand_report_os_fit(void)
{
    struct rt_mtd_nand_device *dev;
    rt_uint8_t hdr[8];
    rt_err_t ret;
    rt_uint32_t magic, totalsize;
    rt_uint32_t part_bytes;

    dev = (struct rt_mtd_nand_device *)rt_device_find("os");
    if (dev == RT_NULL || dev->parent.type != RT_Device_Class_MTD)
    {
        rt_kprintf("os      : not registered as a linear MTD device\n");
        return;
    }

    ret = rt_mtd_nand_read(dev, 0, hdr, sizeof(hdr), RT_NULL, 0);
    if (ret != RT_MTD_EOK && ret != RT_MTD_EECC_CORRECT)
    {
        rt_kprintf("os      : header read failed (%d)\n", ret);
        return;
    }

    magic = ((rt_uint32_t)hdr[0] << 24) | ((rt_uint32_t)hdr[1] << 16) |
            ((rt_uint32_t)hdr[2] << 8) | hdr[3];
    totalsize = ((rt_uint32_t)hdr[4] << 24) | ((rt_uint32_t)hdr[5] << 16) |
                ((rt_uint32_t)hdr[6] << 8) | hdr[7];
    part_bytes = (rt_uint32_t)dev->block_total * dev->pages_per_block *
                 dev->page_size;

    if (magic == SPINAND_FIT_MAGIC && totalsize >= 0x100U &&
        totalsize <= part_bytes)
    {
        rt_kprintf("os      : FIT image detected (magic 0x%08X, "
                   "totalsize %u bytes, partition %u bytes)\n",
                   magic, totalsize, part_bytes);
    }
    else
    {
        rt_kprintf("os      : no valid FIT header (magic 0x%08X, "
                   "totalsize %u, partition %u bytes)\n",
                   magic, totalsize, part_bytes);
    }
}

static void aic_nand_partitions(void)
{
    struct aic_spinand_ro_info info;
    rt_uint32_t i;

    if (aic_spinand_ro_get_info(&info) != RT_EOK)
    {
        rt_kprintf("aic_nand_partitions: SPI NAND is not initialised\n");
        return;
    }

    rt_kprintf("NAND partitions from pack/image_cfg.json\n");
    rt_kprintf("  block size %u bytes, %u blocks total\n",
               (unsigned int)info.page_size * info.pages_per_block,
               info.block_count);
    rt_kprintf("  %-9s %8s %10s %8s %8s %-6s %s\n",
               "name", "start", "offset", "blocks", "bytes", "type", "bad/err");
    for (i = 0; i < SPINAND_PART_COUNT; i++)
    {
        const struct spinand_part_desc *part = &spinand_parts[i];
        rt_uint32_t bytes = (rt_uint32_t)part->block_count *
                            info.pages_per_block * info.page_size;
        rt_uint32_t offset = (rt_uint32_t)part->start_block *
                             info.pages_per_block * info.page_size;
        rt_uint32_t bad = 0, err = 0;

        if (part->linear)
        {
            rt_uint32_t b;
            for (b = 0; b < part->block_count; b++)
            {
                rt_bool_t is_bad = RT_FALSE;
                rt_err_t ret = aic_spinand_ro_check_block(part->start_block + b,
                                                          &is_bad);
                if (ret != RT_EOK)
                    err++;
                else if (is_bad)
                    bad++;
            }
        }

        rt_kprintf("  %-9s %8u 0x%08x %8u %8u %-6s %u/%u\n", part->name,
                   part->start_block, offset, part->block_count, bytes,
                   part->linear ? "MTD" : "NFTL", bad, err);
    }
    rt_kprintf("  data/data_r are NFTL-backed; no linear MTD device is\n"
               "  registered for them.  Raw access is only available through\n"
               "  aic_nand_read_page with physical page numbers.\n");
    rt_kprintf("  MTD page numbers are partition-relative; partition reads\n"
               "  are linear and do not skip bad blocks.  A non-zero bad count\n"
               "  means the linear view may differ from a Bootloader mapping\n"
               "  that skips factory-bad blocks; verify against the image.\n");
    nand_report_os_fit();
}
MSH_CMD_EXPORT_ALIAS(aic_nand_partitions, aic_nand_partitions,
                     list the NAND partition layout and check the os FIT header);

static void aic_nand_read_part(int argc, char **argv)
{
    struct rt_mtd_nand_device *dev;
    rt_uint32_t offset, length, part_bytes;
    rt_uint32_t page_size, pages, done = 0;
    rt_uint8_t *buf;
    rt_uint8_t *pagebuf;
    rt_err_t ret = RT_EOK;

    if (argc != 4)
    {
        rt_kprintf("usage: aic_nand_read_part <name> <offset> <length>\n");
        return;
    }
    if (nand_parse_u32(argv[2], &offset) != 0 ||
        nand_parse_u32(argv[3], &length) != 0)
    {
        rt_kprintf("aic_nand_read_part: bad offset/length\n");
        return;
    }
    if (length == 0)
    {
        rt_kprintf("aic_nand_read_part: length must be non-zero\n");
        return;
    }
    if (length > SPINAND_PART_PRINT_MAX)
    {
        rt_kprintf("aic_nand_read_part: length limited to %u bytes\n",
                   SPINAND_PART_PRINT_MAX);
        return;
    }

    dev = (struct rt_mtd_nand_device *)rt_device_find(argv[1]);
    if (dev == RT_NULL || dev->parent.type != RT_Device_Class_MTD)
    {
        rt_kprintf("aic_nand_read_part: '%s' is not a linear MTD partition "
                   "(NFTL partitions cannot be read as raw blocks)\n", argv[1]);
        return;
    }

    page_size = dev->page_size;
    pages = (rt_uint32_t)dev->block_total * dev->pages_per_block;
    part_bytes = pages * page_size;
    if (offset >= part_bytes || length > part_bytes - offset)
    {
        rt_kprintf("aic_nand_read_part: range 0x%x+0x%x exceeds "
                   "partition '%s' (0x%x bytes)\n",
                   offset, length, argv[1], part_bytes);
        return;
    }

    buf = (rt_uint8_t *)rt_malloc(length);
    pagebuf = (rt_uint8_t *)rt_malloc(page_size);
    if (buf == RT_NULL || pagebuf == RT_NULL)
    {
        rt_kprintf("aic_nand_read_part: out of memory\n");
        rt_free(buf);
        rt_free(pagebuf);
        return;
    }

    while (done < length)
    {
        rt_uint32_t pos = offset + done;
        rt_uint32_t page_index = pos / page_size;
        rt_uint32_t in_page = pos % page_size;
        rt_uint32_t chunk = page_size - in_page;

        if (chunk > length - done)
            chunk = length - done;
        ret = rt_mtd_nand_read(dev, page_index, pagebuf, page_size,
                               RT_NULL, 0);
        if (ret != RT_MTD_EOK && ret != RT_MTD_EECC_CORRECT)
        {
            rt_kprintf("aic_nand_read_part: page %u read failed (%d)\n",
                       page_index, ret);
            break;
        }
        rt_memcpy(buf + done, pagebuf + in_page, chunk);
        done += chunk;
    }

    if (done == length)
    {
        rt_kprintf("partition '%s': read %u bytes at offset 0x%x, crc32 0x%08x\n",
                   argv[1], length, offset, nand_crc32(buf, length));
        nand_hex_dump("data", buf, length, 64);
        rt_kprintf("note: offset is partition-relative; pages are read "
                   "linearly without bad-block remapping\n");
    }
    else
    {
        rt_kprintf("partition '%s': %u of %u bytes read before the error\n",
                   argv[1], done, length);
    }

    rt_free(buf);
    rt_free(pagebuf);
}
MSH_CMD_EXPORT_ALIAS(aic_nand_read_part, aic_nand_read_part,
                     read bytes from a linear MTD partition (read-only));

#endif /* RT_USING_FINSH */

/* ------------------------------------------------------------------ */
/* Initialisation                                                     */
/* ------------------------------------------------------------------ */

static int rt_hw_spinand_ro_init(void)
{
    rt_err_t ret;
    rt_uint8_t pre_id[4] = {0};
    int busy;

    rt_memset(&spinand, 0, sizeof(spinand));
    if (rt_mutex_init(&spinand.lock, "spinand", RT_IPC_FLAG_PRIO) != RT_EOK)
    {
        rt_kprintf("spinand: mutex init failed\n");
        return -RT_ERROR;
    }
    spinand.lock_ready = RT_TRUE;

    ret = spinand_attach_qspi();
    if (ret != RT_EOK)
        return -RT_ERROR;

    /* Read the ID before reset.  This separates an RX/pinmux problem from
     * a reset/status problem in the boot log. */
    if (spinand_read_id_op(pre_id) == RT_EOK)
        rt_kprintf("spinand: pre-reset ID %02X %02X %02X %02X\n",
                   pre_id[0], pre_id[1], pre_id[2], pre_id[3]);
    else
        rt_kprintf("spinand: pre-reset ID read failed\n");

    /* The vendor sequence starts with a software reset. */
    if (spinand_transfer(&cmd_reset, 0, RT_NULL, RT_NULL, 0) != RT_EOK)
    {
        rt_kprintf("spinand: reset command failed\n");
        return -RT_ERROR;
    }
    busy = spinand_wait_busy(RT_NULL);
    if (busy != RT_EOK)
    {
        rt_uint8_t sr = 0;
        if (spinand_read_status_reg(&sr) == RT_EOK)
            rt_kprintf("spinand: reset busy timeout, STATUS=0x%02x\n", sr);
        else
            rt_kprintf("spinand: reset busy timeout and STATUS read failed\n");
        rt_hw_qspi0_dump();
        /* Continue: the ID/geometry/config checks below will report
         * whether the device is actually usable. */
    }

    ret = spinand_probe();
    if (ret != RT_EOK)
        return -RT_ERROR;

    ret = spinand_register_partitions();
    if (ret != RT_EOK)
    {
        /* Raw access is still available for diagnostics. */
        rt_kprintf("spinand: running without MTD partitions\n");
    }
    return RT_EOK;
}
INIT_DEVICE_EXPORT(rt_hw_spinand_ro_init);
