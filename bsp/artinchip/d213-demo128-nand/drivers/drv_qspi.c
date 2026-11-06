/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D21x QSPI0 controller driver (v1.0 register block).
 *
 * Derived from the Luban-Lite D21x QSPI HAL, which is Apache-2.0:
 *   bsp/artinchip/hal/qspi/qspi_hw_v1.0.h
 *   bsp/artinchip/hal/qspi/hal_qspi_v1x.c
 *   bsp/artinchip/hal/qspi/qspi_internal.c
 *   bsp/artinchip/drv/qspi/drv_qspi.c
 *
 * Scope of this port: one QSPI0 master, CPU FIFO, busy-polled
 * synchronous transfers, software chip select.  DMA, interrupts,
 * non-blocking mode and the XIP/display paths are not implemented.
 *
 * NOTE on address.size: upstream or native RT-Thread QSPI callers fill
 * address.size in bits (24/32) while the ArtInChip vendor code uses
 * bytes (3/4).  This driver normalises both: a value > 4 is treated as
 * bits, otherwise as bytes.
 */

#include <rthw.h>
#include <rtthread.h>
#include <rtdevice.h>

#include "drv_qspi.h"
#include "board.h"

#ifndef BSP_QSPI0_INPUT_HZ
#define BSP_QSPI0_INPUT_HZ      100000000UL
#endif

#define AIC_QSPI_SCLK_HZ        BSP_QSPI0_INPUT_HZ
#define AIC_QSPI_FIFO_DEPTH     64U
#define AIC_QSPI_WATERMARK      32U
#define AIC_QSPI_MAX_FREQ       200000000UL
#define AIC_QSPI_MIN_FREQ       100000UL
#define AIC_QSPI_POLL_STEP_US   1U

/* Register offsets, qspi_hw_v1.0.h */
#define QSPI_REG_CFG            0x04U
#define QSPI_REG_TCFG           0x08U
#define QSPI_REG_ICR            0x10U
#define QSPI_REG_ISTS           0x14U
#define QSPI_REG_FCTL           0x18U
#define QSPI_REG_FSTS           0x1CU
#define QSPI_REG_CCFG           0x24U
#define QSPI_REG_TBC            0x30U
#define QSPI_REG_TWC            0x34U
#define QSPI_REG_TMC            0x38U
#define QSPI_REG_TXD            0x200U
#define QSPI_REG_RXD            0x300U

#define CFG_CTRL_EN             (1U << 0)
#define CFG_CTRL_MODE_MASTER    (1U << 1)
#define CFG_RXFULL_STOP         (1U << 7)
#define CFG_CTRL_RST            (1U << 31)

#define TCFG_CPHA               (1U << 0)
#define TCFG_CPOL               (1U << 1)
#define TCFG_CS_POL             (1U << 2)
#define TCFG_CS_NUM_MSK         (0x3U << 4)
#define TCFG_CS_CTL_SEL         (1U << 6)
#define TCFG_CS_LEVEL           (1U << 7)
#define TCFG_DINVD              (1U << 8)
#define TCFG_RXINDLY_EN         (1U << 11)
#define TCFG_LSB_EN             (1U << 12)
#define TCFG_RXDLY_DIS          (1U << 13)
#define TCFG_TXDLY_EN           (1U << 14)
#define TCFG_START              (1UL << 31)

#define ISTS_TDONE              (1U << 12)
#define ISTS_ERRS               ((1U << 10) | (1U << 9) | (1U << 8))
#define ISTS_ALL_MSK            0x3F77U

#define FCTL_RX_RST             (1U << 15)
#define FCTL_TX_RST             (1UL << 31)
#define FCTL_WM_MASK            (0xFFU | (0xFFU << 16))

#define CCFG_CKDIV2_MSK         0xFFU
#define CCFG_CKDIV1_MSK         (0xFU << 8)
#define CCFG_SEL_MSK            (1U << 12)

struct aic_qspi
{
    struct rt_spi_bus bus;
    rt_uint32_t base;
    rt_uint32_t sclk;
    rt_uint32_t bus_hz;
    rt_uint32_t actual_hz;
    rt_bool_t registered;
};

static struct aic_qspi aic_qspi0;

static inline rt_uint32_t qspi_read(rt_uint32_t off)
{
    return *(volatile rt_uint32_t *)(rt_ubase_t)(aic_qspi0.base + off);
}

static inline void qspi_write(rt_uint32_t off, rt_uint32_t val)
{
    *(volatile rt_uint32_t *)(rt_ubase_t)(aic_qspi0.base + off) = val;
}

static inline void qspi_clr_bits(rt_uint32_t off, rt_uint32_t msk)
{
    qspi_write(off, qspi_read(off) & ~msk);
}

static inline void qspi_set_bits(rt_uint32_t off, rt_uint32_t msk)
{
    qspi_write(off, qspi_read(off) | msk);
}

static int qspi_poll_clear(rt_uint32_t off, rt_uint32_t msk, rt_uint32_t tmo_us)
{
    rt_uint32_t waited = 0;

    while (qspi_read(off) & msk)
    {
        if (waited++ >= tmo_us)
            return -RT_ETIMEOUT;
        rt_hw_us_delay(AIC_QSPI_POLL_STEP_US);
    }
    return RT_EOK;
}

static rt_uint32_t qspi_calc_timeout(rt_uint32_t bus_hz, rt_uint32_t len)
{
    rt_uint64_t tmo_us;

    if (bus_hz == 0)
        bus_hz = AIC_QSPI_MIN_FREQ;
    tmo_us = ((rt_uint64_t)len * 8U * 1000000U) / bus_hz;
    tmo_us += 100000; /* 100 ms padding, as in qspi_internal.c */
    return (rt_uint32_t)tmo_us;
}

/* Divider selection, ported from qspi_internal.c */
static rt_uint32_t qspi_best_div(rt_uint32_t sclk, rt_uint32_t bus_hz, rt_uint32_t *div)
{
    rt_uint32_t cdr1_clk, cdr2_clk;
    rt_uint32_t cdr1 = 0;
    int cdr2;

    while ((sclk >> cdr1) > bus_hz && cdr1 < 0xFU)
        cdr1++;

    cdr2 = (int)(sclk / (bus_hz * 2U)) - 1;
    if (cdr2 < 0)
        cdr2 = 0;
    if (cdr2 > 0xFF)
        cdr2 = 0xFF;
    while ((sclk / (2U * (rt_uint32_t)(cdr2 + 1))) > bus_hz && cdr2 < 0xFF)
        cdr2++;

    cdr1_clk = sclk >> cdr1;
    cdr2_clk = sclk / (2U * (rt_uint32_t)(cdr2 + 1));

    if (cdr1_clk == bus_hz)
    {
        *div = cdr1;
        return 0;
    }
    if (cdr2_clk == bus_hz)
    {
        *div = (rt_uint32_t)cdr2;
        return 1;
    }
    if (cdr2_clk < bus_hz && cdr1_clk < bus_hz)
    {
        *div = (cdr2_clk > cdr1_clk) ? (rt_uint32_t)cdr2 : cdr1;
        return (cdr2_clk > cdr1_clk) ? 1 : 0;
    }
    *div = (cdr2_clk < cdr1_clk) ? (rt_uint32_t)cdr2 : cdr1;
    return (cdr2_clk < cdr1_clk) ? 1 : 0;
}

static void qspi_set_clk_div(rt_uint32_t div_sel, rt_uint32_t div)
{
    rt_uint32_t val = qspi_read(QSPI_REG_CCFG);

    val &= ~(CCFG_SEL_MSK | CCFG_CKDIV1_MSK | CCFG_CKDIV2_MSK);
    if (div_sel == 1)
        val |= (div & CCFG_CKDIV2_MSK) | CCFG_SEL_MSK;
    else
        val |= (div & 0xFU) << 8;
    qspi_write(QSPI_REG_CCFG, val);
}

static void qspi_set_bus_freq(rt_uint32_t bus_hz)
{
    rt_uint32_t div, sel, cal;

    if (bus_hz > AIC_QSPI_MAX_FREQ)
        bus_hz = AIC_QSPI_MAX_FREQ;
    else if (bus_hz < AIC_QSPI_MIN_FREQ)
        bus_hz = AIC_QSPI_MIN_FREQ;

    sel = qspi_best_div(aic_qspi0.sclk, bus_hz, &div);
    qspi_set_clk_div(sel, div);
    cal = (sel == 1) ? (aic_qspi0.sclk / (2U * (div + 1U))) : (aic_qspi0.sclk >> div);

    /* RX sampling delay: none <=24MHz, half <=60MHz, one cycle above.
     * Same policy as qspi_hw_freq_to_delay_mode(). */
    qspi_clr_bits(QSPI_REG_TCFG, TCFG_RXINDLY_EN | TCFG_RXDLY_DIS);
    if (cal <= 24000000UL)
        qspi_set_bits(QSPI_REG_TCFG, TCFG_RXDLY_DIS);
    else if (cal > 60000000UL)
        qspi_set_bits(QSPI_REG_TCFG, TCFG_RXINDLY_EN);

    aic_qspi0.bus_hz = bus_hz;
    aic_qspi0.actual_hz = cal;
}

static void qspi_set_bus_width(rt_uint32_t width)
{
    rt_uint32_t val = qspi_read(QSPI_REG_TMC);

    val &= ~((1U << 29) | (1U << 28));
    if (width == 4)
        val |= (1U << 29);
    else if (width == 2)
        val |= (1U << 28);
    qspi_write(QSPI_REG_TMC, val);
}

static rt_uint32_t qspi_get_bus_width(void)
{
    rt_uint32_t val = qspi_read(QSPI_REG_TMC);

    if (val & (1U << 29))
        return 4;
    if (val & (1U << 28))
        return 2;
    return 1;
}

static void qspi_set_cs(rt_bool_t assert)
{
    /* CS0, software controlled. Honour TCFG.CS_POL. */
    rt_uint32_t val = qspi_read(QSPI_REG_TCFG);
    rt_bool_t active_high = (val & TCFG_CS_POL) ? RT_TRUE : RT_FALSE;

    val &= ~TCFG_CS_NUM_MSK;
    if (assert == active_high)
        val |= TCFG_CS_LEVEL;
    else
        val &= ~TCFG_CS_LEVEL;
    qspi_write(QSPI_REG_TCFG, val);
}

static int qspi_reset_fifo(void)
{
    qspi_set_bits(QSPI_REG_FCTL, FCTL_RX_RST | FCTL_TX_RST);
    return qspi_poll_clear(QSPI_REG_FCTL, FCTL_RX_RST | FCTL_TX_RST, 100000U);
}

static rt_uint32_t qspi_tx_fifo_count(void)
{
    return (qspi_read(QSPI_REG_FSTS) >> 16) & 0xFFU;
}

static rt_uint32_t qspi_rx_fifo_count(void)
{
    return qspi_read(QSPI_REG_FSTS) & 0xFFU;
}

static inline void qspi_write_tx(const rt_uint8_t *data, rt_uint32_t len)
{
    while (len--)
        *(volatile rt_uint8_t *)(rt_ubase_t)(aic_qspi0.base + QSPI_REG_TXD) = *data++;
}

static inline void qspi_read_rx(rt_uint8_t *data, rt_uint32_t len)
{
    while (len--)
        *data++ = *(volatile rt_uint8_t *)(rt_ubase_t)(aic_qspi0.base + QSPI_REG_RXD);
}

static int qspi_fifo_write(const rt_uint8_t *data, rt_uint32_t len, rt_uint32_t tmo_us)
{
    rt_uint32_t off = 0;

    while (off < len)
    {
        rt_uint32_t free_cnt = AIC_QSPI_FIFO_DEPTH - qspi_tx_fifo_count();
        rt_uint32_t waited = 0;

        while (free_cnt <= (AIC_QSPI_FIFO_DEPTH >> 3))
        {
            if (waited++ >= tmo_us)
                return -RT_ETIMEOUT;
            rt_hw_us_delay(AIC_QSPI_POLL_STEP_US);
            free_cnt = AIC_QSPI_FIFO_DEPTH - qspi_tx_fifo_count();
        }
        if (free_cnt > len - off)
            free_cnt = len - off;
        qspi_write_tx(&data[off], free_cnt);
        off += free_cnt;
    }

    /* Wait until the last byte has left the TX FIFO. */
    {
        rt_uint32_t waited = 0;
        while (qspi_tx_fifo_count() != 0)
        {
            if (waited++ >= tmo_us)
                return -RT_ETIMEOUT;
            rt_hw_us_delay(AIC_QSPI_POLL_STEP_US);
        }
    }
    return RT_EOK;
}

static int qspi_fifo_read(rt_uint8_t *data, rt_uint32_t len, rt_uint32_t tmo_us)
{
    rt_uint32_t off = 0;
    rt_uint32_t waited = 0;

    while (off < len)
    {
        rt_uint32_t cnt = qspi_rx_fifo_count();

        if (cnt == 0)
        {
            if (waited++ >= tmo_us)
                return -RT_ETIMEOUT;
            rt_hw_us_delay(AIC_QSPI_POLL_STEP_US);
            continue;
        }
        waited = 0;
        if (cnt > len - off)
            cnt = len - off;
        qspi_read_rx(&data[off], cnt);
        off += cnt;
    }
    return RT_EOK;
}

/* Full duplex is only valid in single-line mode (stage-1 scope). */
static int qspi_fifo_write_read(const rt_uint8_t *tx, rt_uint8_t *rx,
                                rt_uint32_t len, rt_uint32_t tmo_us)
{
    rt_uint32_t off = 0;

    while (off < len)
    {
        rt_uint32_t free_cnt = AIC_QSPI_FIFO_DEPTH - qspi_tx_fifo_count();
        rt_uint32_t dolen, waited = 0, cnt;

        while (free_cnt <= (AIC_QSPI_FIFO_DEPTH >> 3))
        {
            if (waited++ >= tmo_us)
                return -RT_ETIMEOUT;
            rt_hw_us_delay(AIC_QSPI_POLL_STEP_US);
            free_cnt = AIC_QSPI_FIFO_DEPTH - qspi_tx_fifo_count();
        }
        dolen = (free_cnt > len - off) ? (len - off) : free_cnt;
        qspi_write_tx(&tx[off], dolen);

        waited = 0;
        while (qspi_rx_fifo_count() < dolen)
        {
            if (waited++ >= tmo_us)
                return -RT_ETIMEOUT;
            rt_hw_us_delay(AIC_QSPI_POLL_STEP_US);
        }
        cnt = qspi_rx_fifo_count();
        if (cnt > dolen)
            cnt = dolen;
        qspi_read_rx(&rx[off], cnt);
        off += cnt;
    }
    return RT_EOK;
}

static void qspi_set_transfer_cnt(rt_uint32_t txlen, rt_uint32_t tx_1line,
                                  rt_uint32_t rxlen, rt_uint32_t dummy_bytes)
{
    rt_uint32_t val;

    val = qspi_read(QSPI_REG_TBC);
    val &= ~0xFFFFFFU;
    val |= (txlen + rxlen + dummy_bytes) & 0xFFFFFFU;
    qspi_write(QSPI_REG_TBC, val);

    val = qspi_read(QSPI_REG_TWC);
    val &= ~0xFFFFFFU;
    val |= txlen & 0xFFFFFFU;
    qspi_write(QSPI_REG_TWC, val);

    val = qspi_read(QSPI_REG_TMC);
    val &= ~0xFFFFFFU;
    val &= ~(0xFU << 24);
    val |= (tx_1line & 0xFFFFFFU) | ((dummy_bytes & 0xFU) << 24);
    qspi_write(QSPI_REG_TMC, val);
}

static int qspi_wait_done(rt_uint32_t tmo_us)
{
    rt_uint32_t waited = 0;

    while (!(qspi_read(QSPI_REG_ISTS) & ISTS_TDONE))
    {
        if (waited++ >= tmo_us)
            return -RT_ETIMEOUT;
        rt_hw_us_delay(AIC_QSPI_POLL_STEP_US);
    }
    return RT_EOK;
}

static int qspi_transfer_sync(const rt_uint8_t *tx, rt_uint8_t *rx, rt_uint32_t len)
{
    rt_uint32_t tmo_us;
    rt_uint32_t sts;
    int ret;

    if (len == 0 || (tx == RT_NULL && rx == RT_NULL))
        return -RT_EINVAL;

    tmo_us = qspi_calc_timeout(aic_qspi0.bus_hz, len) * 10U;
    ret = qspi_reset_fifo();
    if (ret != RT_EOK)
        return ret;

    if (tx != RT_NULL && rx != RT_NULL)
    {
        if (qspi_get_bus_width() != 1)
            return -RT_EIO;
        qspi_clr_bits(QSPI_REG_TCFG, TCFG_DINVD);
        qspi_set_transfer_cnt(len, len, 0, 0);
        qspi_set_bits(QSPI_REG_TCFG, TCFG_START);
        ret = qspi_fifo_write_read(tx, rx, len, tmo_us);
    }
    else if (tx != RT_NULL)
    {
        rt_uint32_t tx_1line = (qspi_get_bus_width() == 1) ? len : 0;
        qspi_set_transfer_cnt(len, tx_1line, 0, 0);
        qspi_set_bits(QSPI_REG_TCFG, TCFG_START);
        ret = qspi_fifo_write(tx, len, tmo_us);
    }
    else
    {
        qspi_set_transfer_cnt(0, 0, len, 0);
        qspi_set_bits(QSPI_REG_TCFG, TCFG_START);
        ret = qspi_fifo_read(rx, len, tmo_us);
    }

    if (ret == RT_EOK)
        ret = qspi_wait_done(tmo_us);

    /* Always restore drop-invalid-data for the next RX-only transfer. */
    qspi_set_bits(QSPI_REG_TCFG, TCFG_DINVD);

    sts = qspi_read(QSPI_REG_ISTS);
    if (sts & ISTS_ERRS)
    {
        rt_kprintf("qspi0: FIFO error, ISTS=0x%08x\n", sts);
        ret = -RT_EIO;
    }
    qspi_write(QSPI_REG_ISTS, sts);
    return ret;
}

static void qspi_hw_init(rt_uint32_t bus_hz)
{
    rt_uint32_t val;

    /* Controller reset: enable, master, stop on RX full, self-clearing reset. */
    qspi_write(QSPI_REG_CFG, CFG_CTRL_EN | CFG_CTRL_MODE_MASTER |
                              CFG_RXFULL_STOP | CFG_CTRL_RST);
    qspi_poll_clear(QSPI_REG_CFG, CFG_CTRL_RST, 100000U);
    qspi_set_bits(QSPI_REG_CFG, CFG_CTRL_EN | CFG_CTRL_MODE_MASTER);

    /* Mode 0, MSB first, CS active low and owned by software. */
    qspi_clr_bits(QSPI_REG_TCFG, TCFG_CPOL | TCFG_CPHA | TCFG_LSB_EN);
    qspi_clr_bits(QSPI_REG_TCFG, TCFG_CS_POL | TCFG_CS_NUM_MSK);
    qspi_set_bits(QSPI_REG_TCFG, TCFG_CS_LEVEL | TCFG_CS_CTL_SEL |
                                TCFG_TXDLY_EN | TCFG_DINVD);

    /* No interrupts: the synchronous path polls ISTS. */
    qspi_clr_bits(QSPI_REG_ICR, ISTS_ALL_MSK);
    qspi_write(QSPI_REG_ISTS, ISTS_ALL_MSK);

    qspi_reset_fifo();
    val = qspi_read(QSPI_REG_FCTL);
    val &= ~FCTL_WM_MASK;
    val |= AIC_QSPI_WATERMARK | (AIC_QSPI_WATERMARK << 16);
    qspi_write(QSPI_REG_FCTL, val);

    qspi_set_bus_freq(bus_hz);
}

static rt_err_t qspi_configure(struct rt_spi_device *device,
                               struct rt_spi_configuration *configuration)
{
    rt_uint32_t bus_hz;
    rt_uint32_t mode;

    if (configuration == RT_NULL)
        return -RT_EINVAL;

    mode = configuration->mode;
    /* RT_SPI_LSB is (0 << 2); LSB mode is the absence of RT_SPI_MSB. */
    if (mode & RT_SPI_MSB)
        qspi_clr_bits(QSPI_REG_TCFG, TCFG_LSB_EN);
    else
        qspi_set_bits(QSPI_REG_TCFG, TCFG_LSB_EN);

    if (mode & RT_SPI_CPHA)
        qspi_set_bits(QSPI_REG_TCFG, TCFG_CPHA);
    else
        qspi_clr_bits(QSPI_REG_TCFG, TCFG_CPHA);

    if (mode & RT_SPI_CPOL)
        qspi_set_bits(QSPI_REG_TCFG, TCFG_CPOL);
    else
        qspi_clr_bits(QSPI_REG_TCFG, TCFG_CPOL);

    if (mode & RT_SPI_CS_HIGH)
        qspi_set_bits(QSPI_REG_TCFG, TCFG_CS_POL);
    else
        qspi_clr_bits(QSPI_REG_TCFG, TCFG_CS_POL);

    bus_hz = (configuration->max_hz == 0) ? 24000000UL : configuration->max_hz;
    if (bus_hz != aic_qspi0.bus_hz)
        qspi_set_bus_freq(bus_hz);

    /* Park CS in the deasserted state for the selected polarity. */
    qspi_set_cs(RT_FALSE);

    (void)device;
    return RT_EOK;
}

/* Big-endian address packer, address.size is normalised to bytes. */
static rt_uint32_t qspi_pack_address(rt_uint32_t addr, rt_uint32_t bytes, rt_uint8_t *dst)
{
    rt_uint32_t i;

    for (i = 0; i < bytes; i++)
        dst[i] = (rt_uint8_t)(addr >> (8U * (bytes - 1U - i)));
    return bytes;
}

static rt_uint32_t qspi_addr_bytes(const struct rt_qspi_message *msg)
{
    rt_uint32_t size = msg->address.size;

    if (size > 4)
        size /= 8;
    return (size > 4) ? 4 : size;
}

static rt_err_t qspi_send_command(const struct rt_qspi_message *msg)
{
    rt_uint32_t single_cnt = 0, rest_cnt = 0;
    rt_uint32_t cmd_rest_bw = 1, dmy_bw = 1, dmy_cnt;
    rt_uint32_t addr_bytes = qspi_addr_bytes(msg);
    rt_uint8_t cmdbuf1[16], cmdbuf2[16];

    if (msg->instruction.qspi_lines == 1)
    {
        cmdbuf1[single_cnt++] = msg->instruction.content;
    }
    else if (msg->instruction.qspi_lines > 1)
    {
        cmdbuf2[rest_cnt++] = msg->instruction.content;
        cmd_rest_bw = msg->instruction.qspi_lines;
        dmy_bw = msg->instruction.qspi_lines;
    }

    if (addr_bytes != 0 && msg->address.qspi_lines == 1)
    {
        single_cnt += qspi_pack_address(msg->address.content, addr_bytes,
                                        &cmdbuf1[single_cnt]);
        dmy_bw = 1;
    }
    else if (addr_bytes != 0 && msg->address.qspi_lines > 1)
    {
        rest_cnt += qspi_pack_address(msg->address.content, addr_bytes,
                                      &cmdbuf2[rest_cnt]);
        cmd_rest_bw = msg->address.qspi_lines;
        dmy_bw = msg->address.qspi_lines;
    }

    /* dummy_cycles is expressed in clock cycles; convert to bytes. */
    dmy_cnt = (dmy_bw * msg->dummy_cycles) / 8U;
    if (dmy_cnt != 0)
    {
        if (dmy_bw == 1)
        {
            if (single_cnt + dmy_cnt > sizeof(cmdbuf1))
                return -RT_EINVAL;
            rt_memset(&cmdbuf1[single_cnt], 0, dmy_cnt);
            single_cnt += dmy_cnt;
        }
        else
        {
            if (rest_cnt + dmy_cnt > sizeof(cmdbuf2))
                return -RT_EINVAL;
            rt_memset(&cmdbuf2[rest_cnt], 0, dmy_cnt);
            rest_cnt += dmy_cnt;
        }
    }

    if (single_cnt > 0)
    {
        qspi_set_bus_width(1);
        if (qspi_transfer_sync(cmdbuf1, RT_NULL, single_cnt) != RT_EOK)
            return -RT_EIO;
    }
    if (rest_cnt > 0)
    {
        qspi_set_bus_width(cmd_rest_bw);
        if (qspi_transfer_sync(cmdbuf2, RT_NULL, rest_cnt) != RT_EOK)
            return -RT_EIO;
    }
    return RT_EOK;
}

static rt_ssize_t qspi_xfer(struct rt_spi_device *device, struct rt_spi_message *message)
{
    const rt_uint8_t *sndb;
    rt_uint8_t *rcvb;
    const struct rt_qspi_message *msg;
    rt_ssize_t ret;

    if (device == RT_NULL || message == RT_NULL)
        return -RT_EINVAL;

    sndb = message->send_buf;
    rcvb = message->recv_buf;
    msg = (const struct rt_qspi_message *)message;

    if (message->cs_take && !(device->config.mode & RT_SPI_NO_CS))
        qspi_set_cs(RT_TRUE);

    if (rcvb != RT_NULL)
    {
        if (qspi_send_command(msg) != RT_EOK)
        {
            ret = -RT_EIO;
            goto out;
        }
        qspi_set_bus_width(msg->qspi_data_lines);
        if (qspi_transfer_sync(RT_NULL, rcvb, message->length) != RT_EOK)
            ret = -RT_EIO;
        else
            ret = (rt_ssize_t)message->length;
    }
    else if (sndb != RT_NULL || msg->instruction.qspi_lines != 0)
    {
        if (qspi_send_command(msg) != RT_EOK)
        {
            ret = -RT_EIO;
            goto out;
        }
        if (message->length > 0)
        {
            qspi_set_bus_width(msg->qspi_data_lines);
            if (qspi_transfer_sync(sndb, RT_NULL, message->length) != RT_EOK)
                ret = -RT_EIO;
            else
                ret = (rt_ssize_t)message->length;
        }
        else
        {
            /* Command-only message: the QSPI core treats a zero return as
             * an error, so report the successful command stage as 1. */
            ret = 1;
        }
    }
    else
    {
        ret = -RT_EINVAL;
    }

out:
    if (message->cs_release)
        qspi_set_cs(RT_FALSE);
    return ret;
}

static const struct rt_spi_ops aic_qspi_ops =
{
    .configure = qspi_configure,
    .xfer = qspi_xfer,
};

rt_err_t rt_hw_qspi0_init(void)
{
    rt_err_t ret;

    if (aic_qspi0.registered)
        return RT_EOK;

    aic_qspi0.base = AIC_QSPI0_BASE;
    aic_qspi0.sclk = AIC_QSPI_SCLK_HZ;
    aic_qspi0.bus_hz = 0;

#ifdef BSP_QSPI0_FREQ
    qspi_hw_init((rt_uint32_t)BSP_QSPI0_FREQ);
#else
    qspi_hw_init(24000000UL);
#endif

    ret = rt_qspi_bus_register(&aic_qspi0.bus, "qspi0", &aic_qspi_ops);
    if (ret != RT_EOK)
    {
        rt_kprintf("qspi0: bus registration failed (%d)\n", ret);
        return ret;
    }

    aic_qspi0.registered = RT_TRUE;
    rt_kprintf("qspi0: registered, base=0x%08x sclk=%lu Hz requested=%lu Hz actual=%lu Hz cmu=0x%08x\n",
               (unsigned int)aic_qspi0.base,
               (unsigned long)aic_qspi0.sclk,
               (unsigned long)aic_qspi0.bus_hz,
               (unsigned long)aic_qspi0.actual_hz,
               (unsigned int)*(volatile rt_uint32_t *)(rt_ubase_t)(AIC_CMU_BASE + 0x0460U));
    return RT_EOK;
}

static int rt_hw_qspi_init(void)
{
    return (rt_hw_qspi0_init() == RT_EOK) ? RT_EOK : -RT_ERROR;
}
INIT_DEVICE_EXPORT(rt_hw_qspi_init);
