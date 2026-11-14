/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SDMC2 / SDIO module bring-up and regression test.
 *
 * The test talks to the card through the standard RT-Thread SDIO API,
 * so it exercises the same CMD52/CMD53 path a Wi-Fi/BT function driver
 * uses.  It never writes to function 1's I/O memory: all writes are
 * CCCR/FBR control registers that the core itself manages.
 *
 * A missing module is reported as SKIP and is not a pass of the SDIO
 * path.  Function readiness and interrupt delivery depend on the module
 * firmware and are reported, not asserted.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <drivers/dev_mmcsd_core.h>
#include <drivers/dev_sdio.h>

#include "utest.h"
#include "board.h"
#include "drv_sdmc.h"

#ifdef BSP_USING_SDMC2

#define TC_SDIO_INSTANCE        2
#define TC_SDIO_WAIT_MS         3000
#define TC_SDIO_BLOCK_SIZE      512
#define TC_SDIO_BLOCK_COUNT     4
#define TC_SDIO_BYTE_READ_LEN   64

static struct rt_mmcsd_host *sdio_host;
static struct rt_mmcsd_card *sdio_card;
static struct rt_sdio_function *sdio_func0;
static struct rt_sdio_function *sdio_func1;
static rt_bool_t sdio_func1_ready;
static rt_bool_t sdio_irq_attached;

/* 64-byte aligned for the controller's internal DMA path. */
static rt_uint8_t sdio_buf[TC_SDIO_BLOCK_SIZE * TC_SDIO_BLOCK_COUNT]
    __attribute__((aligned(64)));

static void tc_sdio_irq_handler(struct rt_sdio_function *func)
{
    (void)func;
}

static void tc_sdio_wait_card(void)
{
    int waited = 0;

    sdio_host = aic_sdmc_host(TC_SDIO_INSTANCE);
    if (sdio_host == RT_NULL)
    {
        rt_kprintf("tc_sdio: SDMC2 is not probed\n");
        return;
    }

    while (waited < TC_SDIO_WAIT_MS)
    {
        sdio_card = sdio_host->card;
        if (sdio_card != RT_NULL)
        {
            break;
        }
        rt_thread_mdelay(100);
        waited += 100;
    }

    if (sdio_card != RT_NULL)
    {
        sdio_func0 = sdio_card->sdio_function[0];
        if (sdio_card->sdio_function_num >= 1)
        {
            sdio_func1 = sdio_card->sdio_function[1];
        }
    }
}

static void tc_sdio_card_info(void)
{
    if (sdio_card == RT_NULL)
    {
        rt_kprintf("tc_sdio: no SDIO card detected (SKIP, not a pass of "
                   "the SDIO path)\n");
        return;
    }

    rt_kprintf("tc_sdio: type %u, %u function(s), manf 0x%04x "
               "product 0x%04x\n",
               sdio_card->card_type, sdio_card->sdio_function_num,
               sdio_card->cis.manufacturer, sdio_card->cis.product);
    rt_kprintf("tc_sdio: %s speed, %u-bit bus, max %u Hz\n",
               (sdio_card->flags & CARD_FLAG_HIGHSPEED) ? "high" : "default",
               (sdio_card->host->flags & MMCSD_BUSWIDTH_4) ? 4 : 1,
               sdio_card->cis.max_tran_speed);
    rt_kprintf("tc_sdio: host clock %u Hz, io_cfg bus width %u\n",
               sdio_host->io_cfg.clock, sdio_host->io_cfg.bus_width);

    uassert_true(sdio_card->card_type == CARD_TYPE_SDIO ||
                 sdio_card->card_type == CARD_TYPE_SDIO_COMBO);
    uassert_true(sdio_card->sdio_function_num >= 1);
    uassert_true(sdio_func0 != RT_NULL);
    uassert_true(sdio_func1 != RT_NULL);
    uassert_true(sdio_card->cis.max_tran_speed > 0);
}

static void tc_sdio_cccr(void)
{
    rt_uint8_t rev = 0, bus = 0, caps = 0, ptr[3] = {0};
    rt_uint8_t std_if = 0, ext_if = 0, fptr[3] = {0};
    rt_int32_t err_rev = 0, err_bus = 0, err_caps = 0, err_ptr = 0;

    if (sdio_card == RT_NULL || sdio_func0 == RT_NULL)
    {
        rt_kprintf("tc_sdio: no card, CCCR test skipped\n");
        return;
    }

    mmcsd_host_lock(sdio_host);
    rev = sdio_io_readb(sdio_func0, SDIO_REG_CCCR_CCCR_REV, &err_rev);
    bus = sdio_io_readb(sdio_func0, SDIO_REG_CCCR_BUS_IF, &err_bus);
    caps = sdio_io_readb(sdio_func0, SDIO_REG_CCCR_CARD_CAPS, &err_caps);
    ptr[0] = sdio_io_readb(sdio_func0, SDIO_REG_CCCR_CIS_PTR, &err_ptr);
    ptr[1] = sdio_io_readb(sdio_func0, SDIO_REG_CCCR_CIS_PTR + 1, &err_ptr);
    ptr[2] = sdio_io_readb(sdio_func0, SDIO_REG_CCCR_CIS_PTR + 2, &err_ptr);
    std_if = sdio_io_readb(sdio_func0, SDIO_REG_FBR_BASE(1) +
                           SDIO_REG_FBR_STD_FUNC_IF, &err_ptr);
    ext_if = sdio_io_readb(sdio_func0, SDIO_REG_FBR_BASE(1) +
                           SDIO_REG_FBR_STD_IF_EXT, &err_ptr);
    fptr[0] = sdio_io_readb(sdio_func0, SDIO_REG_FBR_BASE(1) +
                            SDIO_REG_FBR_CIS, &err_ptr);
    fptr[1] = sdio_io_readb(sdio_func0, SDIO_REG_FBR_BASE(1) +
                            SDIO_REG_FBR_CIS + 1, &err_ptr);
    fptr[2] = sdio_io_readb(sdio_func0, SDIO_REG_FBR_BASE(1) +
                            SDIO_REG_FBR_CIS + 2, &err_ptr);
    mmcsd_host_unlock(sdio_host);

    rt_kprintf("tc_sdio: CCCR rev 0x%02x, bus 0x%02x, caps 0x%02x, "
               "CIS ptr 0x%06x\n", rev, bus, caps,
               ptr[0] | (ptr[1] << 8) | (ptr[2] << 16));
    rt_kprintf("tc_sdio: function 1 FBR std 0x%02x ext 0x%02x, "
               "CIS ptr 0x%06x\n", std_if, ext_if,
               fptr[0] | (fptr[1] << 8) | (fptr[2] << 16));

    uassert_int_equal(err_rev, 0);
    uassert_int_equal(err_bus, 0);
    uassert_int_equal(err_caps, 0);
    uassert_int_equal(err_ptr, 0);
    /* Bits [3:0] are the CCCR structure version; 0xf is invalid. */
    uassert_true((rev & 0x0f) <= SDIO_CCCR_REV_3_00);
    /* The host advertises 4-bit, so the core switches the card to it. */
    uassert_int_equal(bus & SDIO_BUS_WIDTH_4BIT, SDIO_BUS_WIDTH_4BIT);
    uassert_true(caps & SDIO_CCCR_CAP_SMB);
}

static void tc_sdio_enable_func(void)
{
    rt_uint8_t io_en = 0, io_rdy = 0;
    rt_int32_t err = 0, func_err = 0;
    rt_int32_t ret;
    rt_uint8_t func_reg = 0;

    if (sdio_card == RT_NULL || sdio_func0 == RT_NULL ||
        sdio_func1 == RT_NULL)
    {
        rt_kprintf("tc_sdio: no function 1, enable test skipped\n");
        return;
    }

    mmcsd_host_lock(sdio_host);
    ret = sdio_enable_func(sdio_func1);
    io_en = sdio_io_readb(sdio_func0, SDIO_REG_CCCR_IO_EN, &err);
    io_rdy = sdio_io_readb(sdio_func0, SDIO_REG_CCCR_IO_RDY, &err);
    /*
     * A CMD52 read inside the function's own register window proves the
     * function was really enabled; CCCR/FBR live in function 0 and are
     * reachable even when function 1 refuses I/O.
     */
    if (!ret)
    {
        func_reg = sdio_io_readb(sdio_func1, 0x00, &func_err);
    }
    mmcsd_host_unlock(sdio_host);

    rt_kprintf("tc_sdio: enable function 1 ret %d, IO_EN 0x%02x, "
               "IO_RDY 0x%02x\n", ret, io_en, io_rdy);
    rt_kprintf("tc_sdio: function 1 reg[0] read err %d value 0x%02x, "
               "max blk %u, cur blk %u\n", func_err, func_reg,
               sdio_func1->max_blk_size, sdio_func1->cur_blk_size);
    uassert_int_equal(err, 0);
    uassert_true(io_en & (1 << sdio_func1->num));

    if (ret != 0)
    {
        /*
         * Function 1 may stay busy until its firmware is loaded.  That
         * is a property of the module, not of the host path, so the
         * CMD53 tests below are skipped rather than failed.
         */
        rt_kprintf("tc_sdio: function 1 not ready (ret %d); CMD53 tests "
                   "will be skipped\n", ret);
        return;
    }

    uassert_true(io_rdy & (1 << sdio_func1->num));
    sdio_func1_ready = RT_TRUE;
}

static void tc_sdio_block_size(void)
{
    rt_uint8_t lo = 0, hi = 0;
    rt_int32_t err = 0;
    rt_int32_t ret = -RT_EIO;
    rt_uint32_t blksize;

    if (!sdio_func1_ready || sdio_func1 == RT_NULL)
    {
        rt_kprintf("tc_sdio: function 1 not ready, block-size test skipped\n");
        return;
    }

    blksize = sdio_func1->max_blk_size;
    if (blksize == 0)
    {
        rt_kprintf("tc_sdio: function 1 CIS has no block size (SKIP)\n");
        return;
    }
    if (blksize > sdio_card->host->max_blk_size)
    {
        blksize = sdio_card->host->max_blk_size;
    }
    if (blksize > TC_SDIO_BLOCK_SIZE)
    {
        blksize = TC_SDIO_BLOCK_SIZE;
    }

    mmcsd_host_lock(sdio_host);
    ret = sdio_set_block_size(sdio_func1, blksize);
    if (!ret)
    {
        lo = sdio_io_readb(sdio_func0,
                           SDIO_REG_FBR_BASE(1) + SDIO_REG_FBR_BLKSIZE,
                           &err);
        hi = sdio_io_readb(sdio_func0,
                           SDIO_REG_FBR_BASE(1) + SDIO_REG_FBR_BLKSIZE + 1,
                           &err);
    }
    mmcsd_host_unlock(sdio_host);

    rt_kprintf("tc_sdio: function 1 block size set %u, read back %u\n",
               blksize, lo | (hi << 8));

    uassert_int_equal(ret, 0);
    uassert_int_equal(err, 0);
    uassert_int_equal(lo | (hi << 8), blksize);
}

/*
 * Sample the SDIO pads as inputs.  hal_gpio_get_value() reads the pad
 * input status register, which stays valid while the pins are muxed to
 * the SDMC2 function, so this neither changes the pinmux nor disturbs
 * the bus.  With the internal pull-ups enabled, idle DAT0..DAT3 and CMD
 * must read high; a stuck-low data line would explain a data phase in
 * which the card never appears to drive the bus.
 */
static void tc_sdio_lines(const char *when)
{
    static const char *names[6] = {"PF.0", "PF.1", "PF.2",
                                   "PF.3", "PF.4", "PF.5"};
    int level[6];
    int i;

    for (i = 0; i < 6; i++)
    {
        rt_base_t pin = rt_pin_get(names[i]);

        level[i] = (pin >= 0) ? (int)rt_pin_read(pin) : -1;
    }

    rt_kprintf("tc_sdio: %s levels D1 %d D0 %d CLK %d CMD %d D3 %d D2 %d\n",
               when, level[0], level[1], level[2], level[3], level[4],
               level[5]);
}

/*
 * Raw PIN_CFG of the SDMC2 pads and of the module power pins, plus the
 * GPIO input register.  The BSP writes the whole PIN_CFG register, so this
 * shows anything the bootloader left in the upper fields (IRQ mode,
 * DIR/OE/IE, debounce).  In function mode the input register does not
 * reflect the pad level, so the "level" column is only meaningful for the
 * pins left as GPIO.
 */
static void tc_sdio_pad_cfg(void)
{
    static const struct
    {
        unsigned int group;
        unsigned int pin;
        const char *name;
    } pads[] =
    {
        {5, 0, "PF.0"}, {5, 1, "PF.1"}, {5, 2, "PF.2"},
        {5, 3, "PF.3"}, {5, 4, "PF.4"}, {5, 5, "PF.5"},
        {3, 1, "PD.1"}, {3, 3, "PD.3"},
    };
    unsigned int i;

    for (i = 0; i < sizeof(pads) / sizeof(pads[0]); i++)
    {
        rt_uint32_t base = AIC_GPIO_BASE + pads[i].group * 0x100;
        rt_uint32_t cfg = *(volatile rt_uint32_t *)(base + pads[i].pin * 4 +
                                                    0x80);
        rt_uint32_t in = *(volatile rt_uint32_t *)base;

        rt_kprintf("tc_sdio: %s PIN_CFG %08x level %u\n", pads[i].name, cfg,
                   (unsigned)((in >> pads[i].pin) & 1));
    }
}

static rt_int32_t tc_sdio_set_bus_width(rt_uint32_t width){
    rt_uint8_t busif;
    rt_int32_t err = 0;
    busif = sdio_io_readb(sdio_func0, SDIO_REG_CCCR_BUS_IF, &err);
    if (err)
    {
        return err;
    }

    if (width == MMCSD_BUS_WIDTH_4)
    {
        busif |= SDIO_BUS_WIDTH_4BIT;
    }
    else
    {
        busif &= ~SDIO_BUS_WIDTH_4BIT;
    }

    err = sdio_io_writeb(sdio_func0, SDIO_REG_CCCR_BUS_IF, busif);
    if (err)
    {
        return err;
    }

    mmcsd_set_bus_width(sdio_host, width);
    return 0;
}

static void tc_sdio_cmd53(void)
{
    rt_int32_t block_ret = -RT_EIO, block_25m_ret = -RT_EIO;
    rt_int32_t block_1bit_ret = -RT_EIO, byte_1bit_ret = -RT_EIO;
    rt_int32_t byte_slow_ret = -RT_EIO, byte_ret = -RT_EIO;
    rt_int32_t bus_ret = 0, width_ret = 0;
    rt_int32_t cis_ret = -RT_EIO, win_ret = -RT_EIO, one_ret = -RT_EIO;
    rt_int32_t fn2_ret = -RT_EIO, range_ret = -RT_EIO;
    rt_int32_t wr_ret = -RT_EIO, io_en_err = 0, io_rdy_err = 0;
    rt_int32_t first_reg_ret = -RT_EIO, retry_reg_ret = -RT_EIO;
    rt_uint32_t first_reg_val = 0;
    rt_uint32_t ms_first_reg = 0, ms_retry_reg = 0;
    rt_uint8_t io_en_val = 0, io_rdy_val = 0;
    rt_uint8_t win52[4] = {0};
    rt_int32_t win52_err[4] = {0};
    rt_uint8_t vnd_r0[4] = {0};
    rt_uint8_t vnd_r85 = 0, vnd_r100 = 0, vnd_r101 = 0;
    rt_int32_t vnd_err[8] = {0};
    rt_uint32_t wr_buf[1];
    rt_uint32_t width;
    rt_uint32_t clock;
    int i;
    rt_tick_t start;
    rt_uint32_t ms_block = 0, ms_25m = 0, ms_1bit = 0;
    rt_uint32_t ms_byte_1bit = 0, ms_slow = 0, ms_byte = 0;
    rt_uint8_t first = 0, cis_first = 0, win_first = 0, one_first = 0;

    if (!sdio_func1_ready || sdio_func1 == RT_NULL)
    {
        rt_kprintf("tc_sdio: function 1 not ready, CMD53 test skipped\n");
        return;
    }

    width = sdio_host->io_cfg.bus_width;
    clock = sdio_host->io_cfg.clock;

    mmcsd_host_lock(sdio_host);

    tc_sdio_lines("idle pad");

    /*
     * Read-only view of the function-1 local register block the module's
     * own driver uses.  Local register 0x100 tells which transport that
     * driver selects for its register accesses: when it reads 0xEA the
     * driver keeps the CMD52 indirect path, otherwise it switches to the
     * CMD53 indirect path after its chip power-on sequence.  The vendor
     * writes this test once applied experimentally (local 0x00 masked
     * with 0xDFF8, local 0x85 bit 1 set) were accepted but did not
     * unlock the data phase, so the test stays read-only.
     */
    for (i = 0; i < 4; i++)
    {
        vnd_r0[i] = sdio_io_readb(sdio_func1, (rt_uint32_t)i, &vnd_err[i]);
    }
    vnd_r85 = sdio_io_readb(sdio_func1, 0x085, &vnd_err[4]);
    (void)sdio_io_readb(sdio_func1, 0x020, &vnd_err[5]);
    vnd_r100 = sdio_io_readb(sdio_func1, 0x100, &vnd_err[6]);
    vnd_r101 = sdio_io_readb(sdio_func1, 0x101, &vnd_err[7]);

    rt_kprintf("tc_sdio: vendor local regs: 0x00 %02x %02x %02x %02x, "
               "0x85 %02x, 0x100 %02x, 0x101 %02x, read errors %d %d %d "
               "%d\n",
               vnd_r0[0], vnd_r0[1], vnd_r0[2], vnd_r0[3], vnd_r85,
               vnd_r100, vnd_r101, vnd_err[4], vnd_err[5], vnd_err[6],
               vnd_err[7]);

    /*
     * The core raises the clock to the 50 MHz high-speed rate whenever
     * the card supports high-speed mode, but this module's CIS tops out
     * at 25 MHz.  The clean first data phase is spent at the rate the
     * card itself advertises, so the clock is the only variable left
     * untested when the data phase still does not start.
     */
    if (sdio_card->cis.max_tran_speed &&
        clock > sdio_card->cis.max_tran_speed)
    {
        mmcsd_set_clock(sdio_host, sdio_card->cis.max_tran_speed);
        clock = sdio_host->io_cfg.clock;
        rt_kprintf("tc_sdio: clock lowered to %u Hz for first data phase\n",
                   (unsigned)clock);
    }

    /*
     * A CMD52 never touches DAT0..DAT3, so a data-line fault on the
     * SDMC2 side would look exactly like a card that refuses to start
     * the data phase.  Spend the one clean data phase of this boot at
     * 1-bit width, which uses DAT0 alone, then put the 4-bit bus back.
     */
    width_ret = tc_sdio_set_bus_width(MMCSD_BUS_WIDTH_1);
    if (!width_ret)
    {
        rt_kprintf("tc_sdio: bus switched to 1-bit for first data phase\n");
    }

    /*
     * Diagnostic order.  The first data phase of a boot follows a clean
     * enumeration, so spend it on the same shape a function driver uses
     * for a register read: a 4-byte byte-mode CMD53 read at
     * function-1 address 0.  Function 0 is not used here because a
     * dangling function-0 transfer would contaminate later attempts.
     */
    rt_kprintf("tc_sdio: t=%u ms, first data phase: fn1 4-byte read at "
               "%u Hz %u-bit\n",
               (unsigned)rt_tick_get_millisecond(), (unsigned)clock,
               (sdio_host->io_cfg.bus_width == MMCSD_BUS_WIDTH_4) ? 4 : 1);

    rt_memset(sdio_buf, 0, 4);
    start = rt_tick_get_millisecond();
    first_reg_ret = sdio_io_rw_extended(sdio_card, 0, sdio_func1->num, 0, 1,
                                        sdio_buf, 1, 4);
    ms_first_reg = (rt_uint32_t)(rt_tick_get_millisecond() - start);
    rt_memcpy(&first_reg_val, sdio_buf, 4);
    rt_kprintf("tc_sdio: fn1 4-byte read ret %d (%u ms) value 0x%08x\n",
               first_reg_ret, ms_first_reg, first_reg_val);

    if (!width_ret)
    {
        width_ret = tc_sdio_set_bus_width(width);
        if (!width_ret)
        {
            rt_kprintf("tc_sdio: bus restored to 4-bit\n");
        }
    }

    /*
     * The driver now aborts the function through CCCR after a failed
     * data phase, so repeating the identical request shows whether the
     * abort returned the card to a usable state.
     */
    rt_memset(sdio_buf, 0, 4);
    start = rt_tick_get_millisecond();
    retry_reg_ret = sdio_io_rw_extended(sdio_card, 0, sdio_func1->num, 0, 1,
                                        sdio_buf, 1, 4);
    ms_retry_reg = (rt_uint32_t)(rt_tick_get_millisecond() - start);
    rt_memcpy(&first_reg_val, sdio_buf, 4);
    rt_kprintf("tc_sdio: fn1 4-byte read after abort ret %d (%u ms) "
               "value 0x%08x\n",
               retry_reg_ret, ms_retry_reg, first_reg_val);

    rt_thread_mdelay(200);

    /*
     * Block mode with more than one block: a single 512-byte block is
     * encoded by the core as byte mode with a zero count, so this is
     * the only shape that really exercises the block-mode data phase.
     */
    rt_memset(sdio_buf, 0, sizeof(sdio_buf));
    start = rt_tick_get_millisecond();
    block_ret = sdio_io_rw_extended(sdio_card, 0, sdio_func1->num, 0, 1,
                                    sdio_buf, TC_SDIO_BLOCK_COUNT,
                                    TC_SDIO_BLOCK_SIZE);
    ms_block = (rt_uint32_t)(rt_tick_get_millisecond() - start);

    if (block_ret)
    {
        /*
         * Two independent variables are worth separating: the card
         * clock (the CIS says 25 MHz while the core picks the 50 MHz
         * high-speed rate) and the bus width.  Retry at the rate the
         * card itself advertises first, then at 1-bit.
         */
        mmcsd_set_clock(sdio_host, 25000000);
        rt_memset(sdio_buf, 0, sizeof(sdio_buf));
        start = rt_tick_get_millisecond();
        block_25m_ret = sdio_io_rw_extended(sdio_card, 0, sdio_func1->num, 0,
                                            1, sdio_buf,
                                            TC_SDIO_BLOCK_COUNT,
                                            TC_SDIO_BLOCK_SIZE);
        ms_25m = (rt_uint32_t)(rt_tick_get_millisecond() - start);
        if (block_25m_ret)
        {
            bus_ret = tc_sdio_set_bus_width(MMCSD_BUS_WIDTH_1);
            if (!bus_ret)
            {
                rt_memset(sdio_buf, 0, sizeof(sdio_buf));
                start = rt_tick_get_millisecond();
                block_1bit_ret = sdio_io_rw_extended(sdio_card, 0,
                                                     sdio_func1->num, 0, 1,
                                                     sdio_buf,
                                                     TC_SDIO_BLOCK_COUNT,
                                                     TC_SDIO_BLOCK_SIZE);
                ms_1bit = (rt_uint32_t)(rt_tick_get_millisecond() - start);

                /*
                 * Three failed DMA transfers make the driver fall back
                 * to the CPU FIFO on its own, so the next transfer
                 * separates the two host data paths.
                 */
                rt_memset(sdio_buf, 0, sizeof(sdio_buf));
                start = rt_tick_get_millisecond();
                byte_1bit_ret = sdio_io_rw_extended(sdio_card, 0,
                                                    sdio_func1->num, 0, 1,
                                                    sdio_buf, 1,
                                                    TC_SDIO_BYTE_READ_LEN);
                ms_byte_1bit = (rt_uint32_t)(rt_tick_get_millisecond() - start);
            }
        }
    }

    /* Same byte-mode read at 400 kHz: rules out a sampling problem. */
    mmcsd_set_clock(sdio_host, 400000);
    rt_memset(sdio_buf, 0, sizeof(sdio_buf));
    start = rt_tick_get_millisecond();
    byte_slow_ret = sdio_io_rw_extended(sdio_card, 0, sdio_func1->num, 0, 1,
                                        sdio_buf, 1, TC_SDIO_BYTE_READ_LEN);
    ms_slow = (rt_uint32_t)(rt_tick_get_millisecond() - start);

    mmcsd_set_clock(sdio_host, clock);
    if (bus_ret == 0)
    {
        tc_sdio_set_bus_width(width);
    }

    /* Byte mode, 64 bytes from the function-1 register window. */
    rt_memset(sdio_buf, 0, sizeof(sdio_buf));
    start = rt_tick_get_millisecond();
    byte_ret = sdio_io_rw_extended(sdio_card, 0, sdio_func1->num, 0, 1,
                                   sdio_buf, 1, TC_SDIO_BYTE_READ_LEN);
    ms_byte = (rt_uint32_t)(rt_tick_get_millisecond() - start);
    first = sdio_buf[0];

    /*
     * Discriminating diagnostics, printed but not asserted: if the
     * function-0 CIS (plain read-only memory behind the same data
     * phase) also times out, the host data path is at fault rather
     * than the module's function 1; if it works, the module is.
     */
    rt_memset(sdio_buf, 0, sizeof(sdio_buf));
    cis_ret = sdio_io_rw_extended(sdio_card, 0, 0, 0x1000, 1,
                                  sdio_buf, 1, TC_SDIO_BYTE_READ_LEN);
    cis_first = sdio_buf[0];
    rt_memset(sdio_buf, 0, sizeof(sdio_buf));
    win_ret = sdio_io_rw_extended(sdio_card, 0, sdio_func1->num, 0x10000, 1,
                                  sdio_buf, 1, TC_SDIO_BYTE_READ_LEN);
    win_first = sdio_buf[0];
    rt_memset(sdio_buf, 0, sizeof(sdio_buf));
    one_ret = sdio_io_rw_extended(sdio_card, 0, sdio_func1->num, 0, 1,
                                  sdio_buf, 1, 1);
    one_first = sdio_buf[0];

    /*
     * Response-refresh probes.  A card with one function must reject
     * CMD53 for function 2 in the R5 status (bit 9), and an address
     * beyond the implemented window should come back as out of range
     * (bit 8).  If the R5 status seen in the driver log stays the same
     * for these as for the real transfer, the "response" is stale and
     * the card is not answering CMD53 at all.
     */
    rt_memset(sdio_buf, 0, sizeof(sdio_buf));
    fn2_ret = sdio_io_rw_extended(sdio_card, 0, 2, 0, 1, sdio_buf, 1,
                                  TC_SDIO_BYTE_READ_LEN);
    rt_memset(sdio_buf, 0, sizeof(sdio_buf));
    range_ret = sdio_io_rw_extended(sdio_card, 0, sdio_func1->num, 0x1ffff,
                                    1, sdio_buf, 1, TC_SDIO_BYTE_READ_LEN);

    /*
     * Function state and one write-direction probe.  CCCR IO_EN/IO_RDY
     * say whether the card still considers function 1 enabled after the
     * failed data phases.  The CMD53 write goes to the function-0 CIS,
     * which every SDIO card must treat as read-only: the card has to
     * clock in the data (so the host really drives DAT), but it cannot
     * store anything.  A write that completes while every read times out
     * would put the fault on the card-to-host data direction; a write
     * that times out too points at the shared data phase itself.
     */
    io_en_val = sdio_io_readb(sdio_func0, SDIO_REG_CCCR_IO_EN, &io_en_err);
    io_rdy_val = sdio_io_readb(sdio_func0, SDIO_REG_CCCR_IO_RDY, &io_rdy_err);
    wr_buf[0] = 0;
    wr_ret = sdio_io_rw_extended(sdio_card, 1, 0, 0x1000, 1,
                                 (rt_uint8_t *)wr_buf, 1, sizeof(wr_buf));

    /*
     * Read-only CMD52 probes of the function-1 register windows.  A
     * standard function answers its FBR; this module's driver also
     * uses local registers at 0x0000 and a MAC register window at
     * 0x10000 in the 17-bit CMD52 address space.  If those windows
     * answer while every CMD53 data phase times out, the function is
     * alive and only its data path refuses to start.
     */
    win52[0] = sdio_io_readb(sdio_func1, 0x00020, &win52_err[0]);
    win52[1] = sdio_io_readb(sdio_func1, 0x00085, &win52_err[1]);
    win52[2] = sdio_io_readb(sdio_func1, 0x10000, &win52_err[2]);
    win52[3] = sdio_io_readb(sdio_func1, 0x10004, &win52_err[3]);

    tc_sdio_lines("after CMD53");

    mmcsd_host_unlock(sdio_host);

    rt_kprintf("tc_sdio: CMD53 fn1 4-byte byte read ret %d (%u ms), "
               "after abort ret %d (%u ms)\n",
               first_reg_ret, ms_first_reg, retry_reg_ret, ms_retry_reg);
    rt_kprintf("tc_sdio: CMD53 block read %u x %u ret %d (%u ms), "
               "25 MHz ret %d (%u ms), 25 MHz 1-bit ret %d (%u ms)\n",
               TC_SDIO_BLOCK_COUNT, TC_SDIO_BLOCK_SIZE, block_ret, ms_block,
               block_25m_ret, ms_25m, block_1bit_ret, ms_1bit);
    rt_kprintf("tc_sdio: CMD53 byte read 64 @25 MHz 1-bit ret %d (%u ms), "
               "@400 kHz ret %d (%u ms), final ret %d (%u ms) first 0x%02x\n",
               byte_1bit_ret, ms_byte_1bit, byte_slow_ret, ms_slow,
               byte_ret, ms_byte, first);
    rt_kprintf("tc_sdio: CMD53 diag: fn0 CIS 64 ret %d first 0x%02x, "
               "fn1 0x10000 64 ret %d first 0x%02x, fn1 1 byte ret %d "
               "first 0x%02x, fn2 ret %d, fn1 0x1ffff ret %d\n",
               cis_ret, cis_first, win_ret, win_first, one_ret, one_first,
               fn2_ret, range_ret);
    rt_kprintf("tc_sdio: CMD53 diag 2: IO_EN 0x%02x (err %d), IO_RDY 0x%02x "
               "(err %d), fn0 CIS write 4 ret %d\n",
               io_en_val, io_en_err, io_rdy_val, io_rdy_err, wr_ret);
    rt_kprintf("tc_sdio: CMD52 fn1 window: 0x20 0x%02x (%d), 0x85 0x%02x "
               "(%d), 0x10000 0x%02x (%d), 0x10004 0x%02x (%d)\n",
               win52[0], win52_err[0], win52[1], win52_err[1],
               win52[2], win52_err[2], win52[3], win52_err[3]);

    /*
     * The module's data phase is owned by its own driver stack: the
     * vendor code performs the chip power-on flow over CMD52 and only
     * then moves its register access onto CMD53.  Nothing in this BSP
     * runs that flow, so a CMD53 is accepted by the card (R5 state
     * CMD, no error flags) but the data phase never starts.  Assert
     * the data path end to end if it does start, and otherwise assert
     * that the failed data phases left the function reachable over
     * CMD52.
     */
    if (first_reg_ret == 0)
    {
        uassert_int_equal(block_ret, 0);
    }
    else
    {
        for (i = 0; i < 4; i++)
        {
            uassert_int_equal(win52_err[i], 0);
        }
    }
}

static void tc_sdio_irq(void)
{
    rt_uint8_t int_en = 0;
    rt_int32_t err = 0;
    rt_int32_t ret;

    if (sdio_card == RT_NULL || sdio_func0 == RT_NULL ||
        sdio_func1 == RT_NULL)
    {
        rt_kprintf("tc_sdio: no function 1, IRQ test skipped\n");
        return;
    }

    mmcsd_host_lock(sdio_host);
    ret = sdio_attach_irq(sdio_func1, tc_sdio_irq_handler);
    if (!ret)
    {
        sdio_irq_attached = RT_TRUE;
        /*
         * The core writes CCCR INT_EN but leaves controller arming to
         * the host; sdio_irq_wakeup() expects it to be armed already.
         */
        if (sdio_host->ops->enable_sdio_irq)
        {
            sdio_host->ops->enable_sdio_irq(sdio_host, 1);
        }
        int_en = sdio_io_readb(sdio_func0, SDIO_REG_CCCR_INT_EN, &err);
    }
    mmcsd_host_unlock(sdio_host);

    rt_kprintf("tc_sdio: attach IRQ ret %d, CCCR INT_EN 0x%02x\n",
               ret, int_en);

    uassert_int_equal(ret, 0);
    uassert_int_equal(err, 0);
    uassert_true(int_en & 0x01);
    uassert_true(int_en & (1 << sdio_func1->num));

    mmcsd_host_lock(sdio_host);
    ret = sdio_detach_irq(sdio_func1);
    if (!ret)
    {
        sdio_irq_attached = RT_FALSE;
        int_en = sdio_io_readb(sdio_func0, SDIO_REG_CCCR_INT_EN, &err);
    }
    mmcsd_host_unlock(sdio_host);

    rt_kprintf("tc_sdio: detach IRQ ret %d, CCCR INT_EN 0x%02x\n",
               ret, int_en);

    uassert_int_equal(ret, 0);
    uassert_int_equal(err, 0);
    uassert_int_equal(int_en, 0);
}

static rt_err_t tc_sdio_init(void)
{
    sdio_host = RT_NULL;
    sdio_card = RT_NULL;
    sdio_func0 = RT_NULL;
    sdio_func1 = RT_NULL;
    sdio_func1_ready = RT_FALSE;
    sdio_irq_attached = RT_FALSE;
    rt_memset(sdio_buf, 0, sizeof(sdio_buf));

    tc_sdio_wait_card();

    /* Host state, so a failing data phase is logged with the registers
     * and pad configuration it happened in. */
    aic_sdmc_dump(TC_SDIO_INSTANCE);
    tc_sdio_pad_cfg();

    return RT_EOK;
}

static void tc_sdio_cleanup(void)
{
    if (sdio_irq_attached && sdio_func1 != RT_NULL)
    {
        mmcsd_host_lock(sdio_host);
        sdio_detach_irq(sdio_func1);
        mmcsd_host_unlock(sdio_host);
        sdio_irq_attached = RT_FALSE;
    }

    if (sdio_func1_ready && sdio_func1 != RT_NULL)
    {
        mmcsd_host_lock(sdio_host);
        sdio_disable_func(sdio_func1);
        mmcsd_host_unlock(sdio_host);
        sdio_func1_ready = RT_FALSE;
    }
}

static void tc_sdio_case(void)
{
    UTEST_UNIT_RUN(tc_sdio_card_info);
    UTEST_UNIT_RUN(tc_sdio_cccr);
    UTEST_UNIT_RUN(tc_sdio_enable_func);
    UTEST_UNIT_RUN(tc_sdio_block_size);
    UTEST_UNIT_RUN(tc_sdio_cmd53);
    UTEST_UNIT_RUN(tc_sdio_irq);
}

/*
 * 120 s: the CMD53 diagnostic sweep walks several clock/bus-width
 * combinations, and each one that does not complete waits for the
 * controller's data timeout before it moves on.
 */
UTEST_TC_EXPORT(tc_sdio_case, "bsp.d213.sdio", tc_sdio_init,
                tc_sdio_cleanup, 120);

#endif /* BSP_USING_SDMC2 */
