/* SPDX-License-Identifier: Apache-2.0
 * Run the actual NAND adapter with deterministic QSPI responses. No flash
 * or peripheral registers are accessed. The public MTD layout is real.
 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "rtthread.h"
#define RT_USING_DEVICE_OPS
#define RT_ENOMEM 12
#define RT_ENOENT 13
#define RT_Device_Class_MTD 14
#define RT_SPI_MODE_0 0
#define RT_SPI_MSB 4
#define rt_memset memset
#define rt_memcpy memcpy
#define rt_malloc malloc
#define rt_free free
#include <mtd_nand.h>

struct rt_spi_device { struct rt_device parent; };
struct rt_spi_configuration { unsigned int mode, data_width, max_hz; };
struct rt_qspi_configuration {
    struct rt_spi_configuration parent;
    unsigned int medium_size, ddr_mode, qspi_dl_width;
};
struct rt_qspi_device {
    struct rt_spi_device parent;
    struct rt_qspi_configuration config;
};
struct rt_spi_message {
    const void *send_buf;
    void *recv_buf;
    size_t length;
    struct rt_spi_message *next;
    unsigned int cs_take, cs_release;
};
struct rt_qspi_message {
    struct rt_spi_message parent;
    struct { unsigned int content, size, qspi_lines; } instruction, address;
    unsigned int dummy_cycles, qspi_data_lines;
};
rt_ssize_t rt_qspi_transfer_message(struct rt_qspi_device *, struct rt_qspi_message *);
int rt_spi_bus_attach_device(struct rt_spi_device *, const char *, const char *, void *);
int rt_spi_bus_detach_device(struct rt_spi_device *);
int rt_spi_configure(struct rt_spi_device *, struct rt_spi_configuration *);
rt_device_t rt_device_find(const char *);
void rt_hw_us_delay(int);
void rt_kprintf(const char *, ...);

#include "drv_spinand.c"

static unsigned int status_byte, marker_byte = 0xff;
static unsigned int fail_opcode, loaded_page, cache_reads, marker_reads;
static unsigned int delay_count;
static struct rt_qspi_device flash;
static unsigned char cache[2112];

rt_ssize_t rt_qspi_transfer_message(struct rt_qspi_device *dev, struct rt_qspi_message *m)
{
    assert(dev == &flash);
    assert(spinand.lock.depth == 1);
    if (m->instruction.content == fail_opcode) return -RT_EIO;
    switch (m->instruction.content) {
    case SPINAND_CMD_PAGE_READ:
        loaded_page = m->address.content;
        return 1;
    case SPINAND_CMD_GET_FEATURE:
        assert(m->address.content == SPINAND_REG_STATUS);
        *(unsigned char *)m->parent.recv_buf = status_byte;
        return 1;
    case SPINAND_CMD_READ_CACHE:
        cache_reads++;
        if (m->address.content == spinand.page_size) {
            assert(m->parent.length == 1);
            marker_reads++;
            *(unsigned char *)m->parent.recv_buf = marker_byte;
        } else {
            assert(m->address.content == 0 && m->parent.length == sizeof(cache));
            memset(m->parent.recv_buf, 0x5a, m->parent.length);
        }
        return m->parent.length;
    default:
        assert(0); return -RT_EIO;
    }
}
int rt_mutex_take(struct rt_mutex *m, int timeout)
{ (void)timeout; assert(!m->depth); m->depth++; return RT_EOK; }
int rt_mutex_release(struct rt_mutex *m)
{ assert(m->depth == 1); m->depth--; return RT_EOK; }
void rt_hw_us_delay(int us) { assert(us == 1); delay_count++; }
void rt_kprintf(const char *fmt, ...) { (void)fmt; }

int main(void)
{
    rt_bool_t bad;
    struct rt_mtd_nand_device partition = {0};
    unsigned char byte, id[] = {0xef, 0xba, 0x22, 0};
    const struct spinand_model *model;
    int result;
    (void)rt_hw_spinand_ro_init;
    spinand.qspi = &flash;
    spinand.lock_ready = spinand.ready = RT_TRUE;
    spinand.page_size = 2048; spinand.oob_size = 64;
    spinand.block_count = 1024; spinand.pages_per_block = 64;
    spinand.pagebuf = cache; spinand.pagebuf_len = sizeof(cache);
    partition.priv = &spinand;
    partition.block_start = 12; partition.block_total = 32;

    /* A clean marker remains readable even if main-area ECC is bad or
     * reserved. The data-read API must continue to reject those pages. */
    for (unsigned int ecc = 0; ecc < 4; ecc++) {
        status_byte = ecc << 4; marker_byte = 0xff; bad = RT_TRUE;
        assert(aic_spinand_ro_check_block(3, &bad) == RT_EOK && !bad);
        assert(loaded_page == 3 * 64);
        assert(spinand_mtd_check_block(&partition, 1) == RT_EOK);
        assert(loaded_page == 13 * 64);
        marker_byte = 0;
        assert(aic_spinand_ro_check_block(3, &bad) == RT_EOK && bad);
        assert(spinand_mtd_check_block(&partition, 1) == -RT_ERROR);
        byte = 0xcc;
        result = aic_spinand_ro_read_page(0, &byte, 1, NULL, 0, NULL);
        if (ecc < 2) assert(result == (int)ecc && byte == 0x5a);
        else assert(result < 0 && byte == 0xcc);
    }
    assert(marker_reads == 16);
    for (unsigned int i = 0; i < 3; i++) {
        const unsigned int commands[] = {SPINAND_CMD_PAGE_READ,
            SPINAND_CMD_GET_FEATURE, SPINAND_CMD_READ_CACHE};
        fail_opcode = commands[i]; bad = 7;
        assert(aic_spinand_ro_check_block(3, &bad) == -RT_EIO && bad == 7);
        assert(spinand_mtd_check_block(&partition, 1) == RT_MTD_EIO);
        assert(!spinand.lock.depth);
    }
    fail_opcode = 0; status_byte = SPINAND_STATUS_BUSY; bad = 7;
    assert(aic_spinand_ro_check_block(0, &bad) == -RT_ETIMEOUT && bad == 7);
    assert(delay_count == SPINAND_BUSY_TIMEOUT_US);
    assert(spinand_mtd_check_block(&partition, 32) == -RT_MTD_EIO);
    assert(aic_spinand_ro_check_block(1024, &bad) == -RT_EINVAL);
    /* Partial reads copy exactly the requested bytes from the private
     * full-page buffer; invalid lengths never trigger a transport. */
    status_byte = 0;
    { unsigned char guarded[] = {1, 2, 3};
      assert(aic_spinand_ro_read_page(0, guarded + 1, 1, NULL, 0, NULL) == 0);
      assert(guarded[0] == 1 && guarded[1] == 0x5a && guarded[2] == 3);
      unsigned int before = cache_reads;
      assert(aic_spinand_ro_read_page(0, guarded, 4096, NULL, 0, NULL) < 0);
      assert(cache_reads == before);
    }
    model = spinand_match_model(id);
    assert(model && model->block_count == 2048 && model->ecc_strength == 8);
    assert(!model->hw_verified);
    id[1] = 0xaa;
    model = spinand_match_model(id);
    assert(model && model->ecc_strength == 8 && !model->hw_verified);
    return 0;
}
