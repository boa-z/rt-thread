/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D21x UART driver.
 *
 * The register layout and programming sequence are derived from the
 * ArtInChip Luban-Lite HAL (bsp/artinchip/hal/uart/aic_hal_uart.c,
 * Apache-2.0).  This is a small RT-Thread serial-v1 glue layer that
 * keeps the BSP independent from the vendor OS abstraction layer.
 *
 * Supported instances on the demo128 board: UART0/1/2/4.  UART3/5/6/7
 * can be built but require board-specific pinmux entries.
 */

#include <rthw.h>
#include <rtthread.h>
#include <rtdevice.h>
#include <stdint.h>

#include <interrupt.h>
#include "aic_soc.h"
#include "aic_clk_id.h"

#ifdef BSP_USING_UART_DMA
#include "aic_dma_id.h"
#include "drv_dma.h"
#include "hal_dma.h"
#endif

#include "aic_uart_reg.h"

#ifndef AIC_UART_DMA_STAGE_SIZE
#define AIC_UART_DMA_STAGE_SIZE 256
#endif

/* One staged piece (<= 256 bytes) must never take this long, even at
 * 1200 baud.  A stalled DMAC is aborted and reported instead of leaving
 * the serial DMA TX queue wedged forever. */
#ifndef UART_DMA_TX_TIMEOUT_MS
#define UART_DMA_TX_TIMEOUT_MS 5000
#endif

struct aic_uart
{
    uint32_t id;
    uint32_t base;
    uint32_t irq;
    uint32_t clk;
    rt_bool_t rx_thread_started;
    rt_bool_t auto_flow;   /* controller RTS/CTS auto flow control */
    const char *name;
    struct rt_serial_device serial;
#ifdef BSP_USING_UART_DMA
    struct aic_dma_chan *dma_tx_chan;
    const rt_uint8_t *dma_tx_src;
    rt_size_t dma_tx_left;      /* bytes of the current chunk not staged yet */
    rt_tick_t dma_tx_deadline;  /* watchdog deadline for the active piece    */
    volatile rt_bool_t dma_tx_busy;
    /* The HAL requires an 8-byte aligned source.  Always DMA from this
     * aligned stage so caller buffers need no alignment and no size limit;
     * only one transfer is in flight, so one stage per instance is enough. */
    rt_uint8_t dma_tx_stage[AIC_UART_DMA_STAGE_SIZE]
        __attribute__((aligned(AIC_DMA_ALIGN_SIZE)));
#endif
};

#define AIC_UART_INSTANCE(n)                                             \
    {                                                                    \
        .id = (n),                                                       \
        .base = AIC_UART_BASE(n),                                        \
        .irq = AIC_UART_IRQ(n),                                          \
        .clk = BSP_UART_CLK_FREQ,                                        \
    }

#ifdef BSP_USING_UART0
static struct aic_uart aic_uart0 = AIC_UART_INSTANCE(0);
#endif
#ifdef BSP_USING_UART1
static struct aic_uart aic_uart1 = AIC_UART_INSTANCE(1);
#endif
#ifdef BSP_USING_UART2
static struct aic_uart aic_uart2 = AIC_UART_INSTANCE(2);
#endif
#ifdef BSP_USING_UART3
static struct aic_uart aic_uart3 = AIC_UART_INSTANCE(3);
#endif
#ifdef BSP_USING_UART4
static struct aic_uart aic_uart4 = AIC_UART_INSTANCE(4);
#endif
#ifdef BSP_USING_UART5
static struct aic_uart aic_uart5 = AIC_UART_INSTANCE(5);
#endif
#ifdef BSP_USING_UART6
static struct aic_uart aic_uart6 = AIC_UART_INSTANCE(6);
#endif
#ifdef BSP_USING_UART7
/* The BT HCI port is the only one that may need modem flow control. */
static struct aic_uart aic_uart7 =
{
    .id = 7,
    .base = AIC_UART_BASE(7),
    .irq = AIC_UART_IRQ(7),
    .clk = BSP_UART_CLK_FREQ,
#ifdef BSP_UART7_AUTO_FLOW_CTRL
    .auto_flow = RT_TRUE,
#endif
};
#endif

static inline uint32_t uart_read(struct aic_uart *uart, uint32_t reg)
{
    return *(volatile uint32_t *)(uintptr_t)(uart->base + reg);
}

static inline void uart_write(struct aic_uart *uart, uint32_t reg, uint32_t value)
{
    *(volatile uint32_t *)(uintptr_t)(uart->base + reg) = value;
}

/*
 * IER shares its address with DLH, selected by LCR.DLAB.  A bootloader may
 * leave DLAB set; writing IER then silently updates the baud divisor and
 * leaves the real interrupt mask uncontrolled.
 */
static void aic_uart_write_ier(struct aic_uart *uart, uint32_t value)
{
    uint32_t lcr = uart_read(uart, UART_LCR);

    uart_write(uart, UART_LCR, lcr & ~UART_LCR_DLAB);
    /* Make sure the DLAB update is visible before touching the shared IER. */
    (void)uart_read(uart, UART_LCR);
    uart_write(uart, UART_IER, value);
}

static void aic_uart_reset(uint32_t id)
{
    volatile uint32_t *reg = (volatile uint32_t *)(uintptr_t)
        (AIC_CMU_BASE + CLK_UART0_REG + id * 4);
    uint32_t value = *reg;

    /* CMU bit 13 is active-low reset release. */
    *reg = value & ~(1U << 13);
    for (volatile int i = 0; i < 1000; i++)
    {
    }
    *reg = value | (1U << 13);
    for (volatile int i = 0; i < 1000; i++)
    {
    }
}

static uint32_t aic_uart_lcr(const struct serial_configure *cfg)
{
    uint32_t lcr;

    switch (cfg->data_bits)
    {
    case DATA_BITS_5:
        lcr = 0;
        break;
    case DATA_BITS_6:
        lcr = 1;
        break;
    case DATA_BITS_7:
        lcr = 2;
        break;
    default:
        lcr = UART_LCR_8N1;
        break;
    }

    if (cfg->stop_bits == STOP_BITS_2)
        lcr |= UART_LCR_STOP;

    if (cfg->parity == PARITY_ODD)
        lcr |= UART_LCR_PARITY;
    else if (cfg->parity == PARITY_EVEN)
        lcr |= UART_LCR_PARITY | UART_LCR_EVEN;

    return lcr;
}

static rt_err_t aic_uart_configure(struct rt_serial_device *serial,
                                   struct serial_configure *cfg)
{
    struct aic_uart *uart = (struct aic_uart *)serial->parent.user_data;
    uint32_t divisor;
    uint32_t lcr;

    if (cfg->baud_rate == 0 || uart->clk == 0)
    {
        return -RT_EINVAL;
    }

#ifdef BSP_USING_UART_DMA
    /* Reconfiguring the divisor would corrupt the active DRQ transfer. */
    if (uart->dma_tx_busy)
    {
        return -RT_EBUSY;
    }
#endif

    divisor = (uart->clk + (cfg->baud_rate * 8)) / (cfg->baud_rate * 16);
    if (divisor == 0)
    {
        divisor = 1;
    }

    lcr = aic_uart_lcr(cfg);

    /* RX is polled; never leave a controller interrupt enabled. */
    aic_uart_write_ier(uart, 0);

    uart_write(uart, UART_LCR, lcr | UART_LCR_DLAB);
    uart_write(uart, UART_DLL, divisor & 0xff);
    uart_write(uart, UART_DLH, (divisor >> 8) & 0xff);
    uart_write(uart, UART_LCR, lcr);

    /* RS-232 mode.  Only the BT HCI port enables modem flow control;
     * everything else keeps MCR clear. */
    uart_write(uart, UART_MCR, uart->auto_flow ? UART_MCR_AUTO_FLOW : 0);

    /* Enable and reset FIFOs.  A one-byte RX trigger avoids relying on the
     * character-timeout path. */
    uart_write(uart, UART_FCR, UART_FCR_ENABLE | UART_FCR_RX_RESET |
                               UART_FCR_TX_RESET | UART_FCR_RX_TRIG(0) |
                               UART_FCR_TX_TRIG(3));

    /* Enable the receiver. */
    uart_write(uart, UART_RXCTL, UART_RXCTL_ENABLE);

    return RT_EOK;
}

static void aic_uart_start_rx_poll(struct aic_uart *uart);
#ifdef BSP_USING_UART_DMA
static rt_bool_t aic_uart_dma_tx_abort(struct aic_uart *uart);
#endif

static rt_err_t aic_uart_control(struct rt_serial_device *serial,
                                 int cmd, void *arg)
{
    struct aic_uart *uart = (struct aic_uart *)serial->parent.user_data;

    switch (cmd)
    {
    case RT_DEVICE_CTRL_CLR_INT:
#ifdef BSP_USING_UART_DMA
        /*
         * rt_serial_close() calls this before it frees the DMA TX data
         * queue.  Drop an in-flight transfer now: its completion must not
         * signal a queue that is about to disappear.
         */
        if ((rt_ubase_t)arg == RT_DEVICE_FLAG_DMA_TX)
        {
            aic_uart_dma_tx_abort(uart);
        }
#endif
        aic_uart_write_ier(uart, 0);
        rt_hw_interrupt_mask(uart->irq);
        break;

    case RT_DEVICE_CTRL_SET_INT:
        /* RX is polled; do not enable the UART/PLIC interrupt line. */
        aic_uart_write_ier(uart, 0);
        rt_hw_interrupt_mask(uart->irq);
        aic_uart_start_rx_poll(uart);
        break;

    case RT_DEVICE_CTRL_CONFIG:
#ifdef BSP_USING_UART_DMA
        /*
         * rt_serial_open() calls control(RT_DEVICE_CTRL_CONFIG) with the
         * DMA flag itself as the argument.  There is nothing to program
         * here: the DRQ path is armed per transfer.
         */
        if ((rt_ubase_t)arg == RT_DEVICE_FLAG_DMA_TX)
        {
            return RT_EOK;
        }
#endif
        if (arg)
        {
            return aic_uart_configure(serial, (struct serial_configure *)arg);
        }
        break;

    default:
        break;
    }

    return RT_EOK;
}

static int aic_uart_putc(struct rt_serial_device *serial, char c)
{
    struct aic_uart *uart = (struct aic_uart *)serial->parent.user_data;

    while (!(uart_read(uart, UART_LSR) & UART_LSR_THRE));

    uart_write(uart, UART_THR, (uint8_t)c);
    return 1;
}

static int aic_uart_getc(struct rt_serial_device *serial)
{
    struct aic_uart *uart = (struct aic_uart *)serial->parent.user_data;
    uint32_t lsr;

    lsr = uart_read(uart, UART_LSR);
    if (lsr & (UART_LSR_OE | UART_LSR_PE | UART_LSR_FE | UART_LSR_BI))
    {
        /* Reading LSR clears the error flags.  The byte, if any, is still
         * consumed below. */
    }

    if (lsr & UART_LSR_DR)
    {
        return (int)(uart_read(uart, UART_RBR) & 0xff);
    }

    return -1;
}

#ifdef BSP_USING_UART_DMA
static void aic_uart_tx_polled(struct aic_uart *uart, const rt_uint8_t *buf,
                               rt_size_t size)
{
    rt_size_t i;

    for (i = 0; i < size; i++)
    {
        while (!(uart_read(uart, UART_LSR) & UART_LSR_THRE));
        uart_write(uart, UART_THR, buf[i]);
    }
}

/*
 * Stop the active transfer without telling the serial framework.  Used
 * when the device is closed or reconfigured: the DMA TX data queue is
 * about to be freed, so its completion must not be signalled any more.
 */
static rt_bool_t aic_uart_dma_tx_abort(struct aic_uart *uart)
{
    struct aic_dma_chan *chan;
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    if (!uart->dma_tx_busy)
    {
        rt_hw_interrupt_enable(level);
        return RT_FALSE;
    }
    /* Clear the flag first: a late completion IRQ then returns early. */
    uart->dma_tx_busy = RT_FALSE;
    chan = uart->dma_tx_chan;
    uart->dma_tx_chan = RT_NULL;
    uart->dma_tx_left = 0;
    rt_hw_interrupt_enable(level);

    uart_write(uart, UART_HALT,
               uart_read(uart, UART_HALT) & ~UART_HALT_TX_DRQ);
    if (chan)
    {
        hal_dma_chan_stop(chan);
        hal_release_dma_chan(chan);
    }

    return RT_TRUE;
}

/*
 * Copy the next stage-sized piece of the caller's chunk into the aligned
 * stage buffer and start one device transfer.  Must be called with the
 * channel already owned by this UART.
 */
static int aic_uart_dma_tx_kick(struct aic_uart *uart)
{
    struct dma_slave_config cfg;
    rt_size_t piece;
    int ret;

    piece = uart->dma_tx_left > AIC_UART_DMA_STAGE_SIZE
                ? AIC_UART_DMA_STAGE_SIZE : uart->dma_tx_left;
    rt_memcpy(uart->dma_tx_stage, uart->dma_tx_src, piece);
    uart->dma_tx_src += piece;
    uart->dma_tx_left -= piece;

    rt_memset(&cfg, 0, sizeof(cfg));
    cfg.direction = DMA_MEM_TO_DEV;
    cfg.src_addr = (unsigned long)uart->dma_tx_stage;
    cfg.dst_addr = (unsigned long)(uart->base + UART_THR);
    cfg.src_addr_width = DMA_SLAVE_BUSWIDTH_UNDEFINED;
    cfg.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
    cfg.src_maxburst = 1;
    cfg.dst_maxburst = 1;
    cfg.slave_id = DMA_ID_UART0 + uart->id;

    ret = hal_dma_chan_config(uart->dma_tx_chan, &cfg);
    if (!ret)
    {
        /* This also cleans the stage from the D-Cache. */
        ret = hal_dma_chan_prep_device(uart->dma_tx_chan,
                                       (u32)cfg.dst_addr,
                                       (u32)cfg.src_addr, (u32)piece,
                                       DMA_MEM_TO_DEV);
    }
    if (ret)
    {
        return ret;
    }

    /*
     * Program DMA mode once per transfer; never reset the TX/RX FIFOs
     * here.  A TX reset would discard bytes that the previous transfer
     * left in the FIFO when chunks are handed over back to back, and an
     * RX reset would corrupt concurrent receive data.
     */
    uart_write(uart, UART_FCR, UART_FCR_ENABLE | UART_FCR_DMA_MODE |
                               (3U << 4) | (2U << 6));
    uart_write(uart, UART_HSK, UART_HSK_DMA_MODE);
    uart_write(uart, UART_HALT,
               uart_read(uart, UART_HALT) | UART_HALT_TX_DRQ);

    ret = hal_dma_chan_start(uart->dma_tx_chan);
    if (!ret)
    {
        uart->dma_tx_deadline = rt_tick_get_millisecond() +
                                UART_DMA_TX_TIMEOUT_MS;
    }

    return ret;
}

static void aic_uart_dma_tx_done(void *param)
{
    struct aic_uart *uart = (struct aic_uart *)param;
    struct aic_dma_chan *chan;

    /*
     * A transfer aborted by close()/the test clears dma_tx_busy first, so
     * a completion that was already pending must not touch the queue.
     */
    if (!uart->dma_tx_busy)
    {
        return;
    }
    chan = uart->dma_tx_chan;
    if (!chan)
    {
        return;
    }

    /* Close the DRQ source before touching the channel. */
    uart_write(uart, UART_HALT,
               uart_read(uart, UART_HALT) & ~UART_HALT_TX_DRQ);
    hal_dma_chan_stop(chan);

    /* More of the caller's chunk to send?  Keep the channel and go on. */
    if (uart->dma_tx_left)
    {
        if (!aic_uart_dma_tx_kick(uart))
        {
            return;
        }

        /*
         * Restarting on the owned channel failed.  Push the rest out
         * through the FIFO rather than losing it; this is an error path.
         */
        aic_uart_tx_polled(uart, uart->dma_tx_src, uart->dma_tx_left);
        uart->dma_tx_left = 0;
    }

    uart->dma_tx_chan = RT_NULL;
    uart->dma_tx_busy = RT_FALSE;
    hal_release_dma_chan(chan);

    /* Lets the serial framework free the chunk and start the next one. */
    rt_hw_serial_isr(&uart->serial, RT_SERIAL_EVENT_TX_DMADONE);
}

/**
 * UART TX through the shared DMAC.
 *
 * The bytes are staged into an aligned per-instance buffer, so a caller
 * buffer needs neither alignment nor a size limit.  The serial framework
 * calls this for the first chunk in thread context and for the following
 * queued chunks from the completion ISR; both paths only copy and start
 * DMA, they never wait for the wire.
 */
static rt_ssize_t aic_uart_dma_transmit(struct rt_serial_device *serial,
                                        rt_uint8_t *buf, rt_size_t size,
                                        int direction)
{
    struct aic_uart *uart = (struct aic_uart *)serial->parent.user_data;
    struct aic_dma_chan *chan;
    int ret;

    if (direction != RT_SERIAL_DMA_TX || size == 0)
    {
        return 0;
    }

    /*
     * The framework serialises chunks, so this only happens if a caller
     * drives dma_transmit directly.  Keep the data flowing the slow way.
     */
    if (uart->dma_tx_busy)
    {
        aic_uart_tx_polled(uart, buf, size);
        rt_hw_serial_isr(serial, RT_SERIAL_EVENT_TX_DMADONE);
        return size;
    }

    chan = hal_request_dma_chan();
    if (!chan)
    {
        /* All eight channels busy: same fallback, never drop data. */
        aic_uart_tx_polled(uart, buf, size);
        rt_hw_serial_isr(serial, RT_SERIAL_EVENT_TX_DMADONE);
        return size;
    }

    ret = hal_dma_chan_register_cb(chan, aic_uart_dma_tx_done, uart);
    if (ret)
    {
        hal_release_dma_chan(chan);
        aic_uart_tx_polled(uart, buf, size);
        rt_hw_serial_isr(serial, RT_SERIAL_EVENT_TX_DMADONE);
        return size;
    }

    uart->dma_tx_chan = chan;
    uart->dma_tx_src = buf;
    uart->dma_tx_left = size;
    uart->dma_tx_busy = RT_TRUE;

    ret = aic_uart_dma_tx_kick(uart);
    if (ret)
    {
        rt_size_t done;

        uart->dma_tx_busy = RT_FALSE;
        uart->dma_tx_chan = RT_NULL;
        /* kick() consumes the stage before starting: send the rest. */
        done = size - uart->dma_tx_left;
        uart->dma_tx_left = 0;
        uart_write(uart, UART_HALT,
                   uart_read(uart, UART_HALT) & ~UART_HALT_TX_DRQ);
        hal_release_dma_chan(chan);
        aic_uart_tx_polled(uart, buf + done, size - done);
        rt_hw_serial_isr(serial, RT_SERIAL_EVENT_TX_DMADONE);
    }

    return size;
}
#endif /* BSP_USING_UART_DMA */

static const struct rt_uart_ops aic_uart_ops = {
    .configure = aic_uart_configure,
    .control = aic_uart_control,
    .putc = aic_uart_putc,
    .getc = aic_uart_getc,
#ifdef BSP_USING_UART_DMA
    .dma_transmit = aic_uart_dma_transmit,
#else
    .dma_transmit = RT_NULL,
#endif
};

/*
 * D21x UART0 keeps IRQ 76 pending even when the controller reports no
 * source.  The vendor console therefore uses RX polling.  Poll LSR.DR
 * and feed the serial framework from a thread; this avoids the PLIC path
 * entirely while still using the standard serial RX FIFO and rx_indicate.
 */
static void aic_uart_rx_poll(void *parameter)
{
    struct aic_uart *uart = (struct aic_uart *)parameter;

    for (;;)
    {
        if (uart->serial.serial_rx != RT_NULL &&
            (uart_read(uart, UART_LSR) & UART_LSR_DR))
        {
            rt_hw_serial_isr(&uart->serial, RT_SERIAL_EVENT_RX_IND);
        }

        rt_thread_mdelay(1);
    }
}

static void aic_uart_start_rx_poll(struct aic_uart *uart)
{
    rt_thread_t tid;

    if (uart->rx_thread_started)
    {
        return;
    }

    tid = rt_thread_create(uart->name, aic_uart_rx_poll, uart, 1024, 25, 10);
    if (tid != RT_NULL)
    {
        uart->rx_thread_started = RT_TRUE;
        rt_thread_startup(tid);
    }
}

static void aic_uart_register(struct aic_uart *uart, const char *name)
{
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;

    /*
     * Reset the controller before reusing it.  The bootloader may leave a
     * latched interrupt (THRE in particular) that IER writes cannot clear,
     * which makes the PLIC deliver IRQ 76 forever.
     */
    aic_uart_reset(uart->id);

    uart->name = name;
    uart->serial.ops = &aic_uart_ops;
    uart->serial.config = config;

    /* Translate the SoC PLIC hardware IRQ to the RT-Thread handler index. */
    uart->irq = IRQ_PLIC(uart->irq);

    rt_hw_serial_register(&uart->serial, name,
                          RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX
#ifdef BSP_USING_UART_DMA
                          | RT_DEVICE_FLAG_DMA_TX
#endif
                          , uart);

    /*
     * Keep the controller and PLIC interrupt lines disabled and feed the
     * serial framework from the polling thread instead.  The poller is
     * started from RT_DEVICE_CTRL_SET_INT, after the scheduler is running.
     */
    aic_uart_write_ier(uart, 0);
    rt_hw_interrupt_mask(uart->irq);
}

#if defined(BSP_USING_UART1) && defined(BSP_USING_UART_DMA)
#define UART_DMA_TEST_MIN_BYTES  16
#define UART_DMA_TEST_MAX_BYTES  (8 * 1024)
#define UART_DMA_TEST_TIMEOUT_MS 5000

rt_err_t aic_uart1_dma_loopback_test(rt_uint32_t len, rt_uint32_t *kbps)
{
    struct aic_uart *uart = &aic_uart1;
    rt_device_t dev;
    rt_uint8_t *tx = RT_NULL;
    rt_uint8_t *rx = RT_NULL;
    rt_size_t got = 0;
    rt_tick_t start, deadline;
    rt_uint32_t i;
    rt_bool_t same = RT_TRUE;
    rt_err_t ret = RT_EOK;

    if (len < UART_DMA_TEST_MIN_BYTES || len > UART_DMA_TEST_MAX_BYTES)
    {
        return -RT_EINVAL;
    }

    dev = rt_device_find("uart1");
    if (!dev)
    {
        return -RT_ENOSYS;
    }

    /* Drop the channel of a transfer that was interrupted by an earlier
     * failure, so the device can be configured and opened again. */
    aic_uart_dma_tx_abort(uart);

    /*
     * Open with DMA TX only.  With INT_RX the 1 ms RX poll thread moves
     * idle-line bytes into the software ring asynchronously, and one of
     * them can slip in between the flush and the transfer.  This test
     * reads the hardware FIFO directly instead, so the receive side has
     * a single owner.
     */
    ret = rt_device_open(dev, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_DMA_TX);
    if (ret != RT_EOK)
    {
        return ret;
    }

    tx = aic_dma_alloc(len);
    rx = aic_dma_alloc(len);
    if (!tx || !rx)
    {
        ret = -RT_ENOMEM;
        goto out_close;
    }

    for (i = 0; i < len; i++)
    {
        tx[i] = (rt_uint8_t)(i * 13 + 0x3c);
    }
    rt_memset(rx, 0, len);

    /* MCR loopback routes TXD back to RXD inside the controller, so no
     * external wiring (or RS-232 peer) is needed.  Let a byte that was
     * being received before the switch complete, then drain the FIFO. */
    uart_write(uart, UART_MCR,
               uart_read(uart, UART_MCR) | UART_MCR_LOOPBACK);
    rt_thread_mdelay(2);
    while (aic_uart_getc(&uart->serial) >= 0)
    {
    }

    start = rt_tick_get_millisecond();
    deadline = rt_tick_get() + rt_tick_from_millisecond(UART_DMA_TEST_TIMEOUT_MS);

    if (rt_device_write(dev, 0, tx, len) != len)
    {
        ret = -RT_EIO;
        goto out;
    }

    /*
     * Receive while the DMA is still transmitting, straight from the
     * hardware FIFO.  The serial RX ring is only RT_SERIAL_RB_BUFSZ
     * bytes and would need a consumer thread running at line rate.
     */
    while (got < len && rt_tick_get() < deadline)
    {
        int c = aic_uart_getc(&uart->serial);

        if (c < 0)
        {
            rt_thread_mdelay(1);
        }
        else
        {
            rx[got++] = (rt_uint8_t)c;
        }
    }
    if (got != len)
    {
        rt_kprintf("uart_dma_test: received %u of %u bytes\n",
                   (rt_uint32_t)got, len);
        ret = -RT_ETIMEOUT;
        goto out;
    }

    while (uart->dma_tx_busy && rt_tick_get() < deadline)
    {
        rt_thread_mdelay(1);
    }
    if (uart->dma_tx_busy)
    {
        u32 left = 0;
        int ch = uart->dma_tx_chan ? uart->dma_tx_chan->ch_nr : -1;

        if (uart->dma_tx_chan)
        {
            hal_dma_chan_tx_status(uart->dma_tx_chan, &left);
            hal_dma_chan_dump(ch);
        }
        rt_kprintf("uart_dma_test: completion timeout, ch %d left %u\n",
                   ch, left);
        rt_kprintf("uart%d: LSR 0x%x HALT 0x%x HSK 0x%x RFL 0x%x MCR 0x%x "
                   "IIR 0x%x\n", uart->id, uart_read(uart, UART_LSR),
                   uart_read(uart, UART_HALT), uart_read(uart, UART_HSK),
                   uart_read(uart, UART_RFL), uart_read(uart, UART_MCR),
                   uart_read(uart, UART_IIR));
        ret = -RT_ETIMEOUT;
        goto out;
    }

    /* The DMAC only writes the FIFO; wait for the last stop bit. */
    while (!(uart_read(uart, UART_LSR) & UART_LSR_TEMT) &&
           rt_tick_get() < deadline)
    {
        rt_thread_mdelay(1);
    }

    while (got < len && rt_tick_get() < deadline)
    {
        int c = aic_uart_getc(&uart->serial);

        if (c < 0)
        {
            rt_thread_mdelay(1);
        }
        else
        {
            rx[got++] = (rt_uint8_t)c;
        }
    }
    if (got != len)
    {
        rt_kprintf("uart_dma_test: received %u of %u bytes\n",
                   (rt_uint32_t)got, len);
        ret = -RT_ETIMEOUT;
        goto out;
    }

    for (i = 0; i < len; i++)
    {
        if (tx[i] != rx[i])
        {
            rt_kprintf("uart_dma_test: mismatch at %u (tx 0x%02x rx 0x%02x)\n",
                       i, tx[i], rx[i]);
            same = RT_FALSE;
            break;
        }
    }
    if (!same)
    {
        ret = -RT_EIO;
        goto out;
    }

    if (kbps)
    {
        rt_uint32_t elapsed = rt_tick_get_millisecond() - start;

        *kbps = elapsed ? (rt_uint32_t)(((rt_uint64_t)len * 1000) /
                                        (elapsed * 1024)) : 0;
    }

out:
    aic_uart_dma_tx_abort(uart);
    uart_write(uart, UART_MCR,
               uart_read(uart, UART_MCR) & ~UART_MCR_LOOPBACK);
    /* Leave the FIFO in the state the normal polled path expects. */
    uart_write(uart, UART_FCR, UART_FCR_ENABLE | UART_FCR_RX_RESET |
                               UART_FCR_TX_RESET | UART_FCR_RX_TRIG(0) |
                               UART_FCR_TX_TRIG(3));
out_close:
    if (tx)
    {
        aic_dma_free(tx);
    }
    if (rx)
    {
        aic_dma_free(rx);
    }
    rt_device_close(dev);

    return ret;
}

/*
 * Multi-chunk loopback regression.  A writer thread queues up to the
 * serial DMA queue depth without waiting, while this thread drains the
 * receive FIFO.  That is the back-to-back path driven by the completion
 * ISR, and with odd chunk sizes it also makes every chunk after the
 * first start at an unaligned address.  With hold_channels set, every
 * DMAC channel is taken by the test first, so the driver must fall back
 * to polled writes and still deliver the whole stream.
 */
struct aic_uart_dma_writer
{
    rt_device_t dev;
    const rt_uint8_t *buf;
    rt_uint32_t total;
    rt_uint32_t chunk;
    volatile rt_bool_t stop;
    volatile rt_bool_t done;
    rt_err_t err;
};

static void aic_uart_dma_writer_entry(void *param)
{
    struct aic_uart_dma_writer *w = (struct aic_uart_dma_writer *)param;
    rt_uint32_t off = 0;

    while (off < w->total && !w->stop)
    {
        rt_size_t n = w->total - off;

        if (n > w->chunk)
        {
            n = w->chunk;
        }
        if (rt_device_write(w->dev, 0, w->buf + off, n) != (rt_ssize_t)n)
        {
            w->err = -RT_EIO;
            break;
        }
        off += n;
    }
    w->done = RT_TRUE;
}

static void aic_uart_dma_test_prepare(struct aic_uart *uart)
{
    uart_write(uart, UART_MCR,
               uart_read(uart, UART_MCR) | UART_MCR_LOOPBACK);
    rt_thread_mdelay(2);
    while (aic_uart_getc(&uart->serial) >= 0)
    {
    }
}

static void aic_uart_dma_test_finish(struct aic_uart *uart)
{
    aic_uart_dma_tx_abort(uart);
    uart_write(uart, UART_MCR,
               uart_read(uart, UART_MCR) & ~UART_MCR_LOOPBACK);
    /* Leave the FIFO in the state the normal polled path expects. */
    uart_write(uart, UART_FCR, UART_FCR_ENABLE | UART_FCR_RX_RESET |
                               UART_FCR_TX_RESET | UART_FCR_RX_TRIG(0) |
                               UART_FCR_TX_TRIG(3));
}

rt_err_t aic_uart1_dma_loopback_stream(rt_uint32_t total, rt_uint32_t chunk,
                                       rt_bool_t hold_channels,
                                       rt_uint32_t *kbps)
{
    struct aic_uart *uart = &aic_uart1;
    /*
     * The writer thread may outlive a failed run, so its state cannot
     * live on this stack.  utest runs one case at a time.
     */
    static struct aic_uart_dma_writer w;
    struct aic_dma_chan *held[AIC_DMA_CH_NUM];
    rt_device_t dev;
    rt_thread_t tid;
    rt_uint8_t *tx = RT_NULL;
    rt_uint8_t *rx = RT_NULL;
    rt_size_t got = 0;
    rt_tick_t start, deadline;
    rt_uint32_t i, nheld = 0;
    rt_bool_t same = RT_TRUE;
    rt_err_t ret = RT_EOK;

    if (total < UART_DMA_TEST_MIN_BYTES || total > UART_DMA_TEST_MAX_BYTES ||
        chunk == 0 || chunk > total)
    {
        return -RT_EINVAL;
    }
    /* Keep the writer below the serial DMA queue depth (8) so its pushes
     * never block; a stalled driver then fails the test, not the thread. */
    if ((total + chunk - 1) / chunk > 8)
    {
        return -RT_EINVAL;
    }

    dev = rt_device_find("uart1");
    if (!dev)
    {
        return -RT_ENOSYS;
    }

    aic_uart_dma_tx_abort(uart);
    ret = rt_device_open(dev, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_DMA_TX);
    if (ret != RT_EOK)
    {
        return ret;
    }

    tx = aic_dma_alloc(total);
    rx = aic_dma_alloc(total);
    if (!tx || !rx)
    {
        ret = -RT_ENOMEM;
        goto out_close;
    }

    for (i = 0; i < total; i++)
    {
        tx[i] = (rt_uint8_t)(i * 13 + 0x3c);
    }
    rt_memset(rx, 0, total);
    aic_uart_dma_test_prepare(uart);

    if (hold_channels)
    {
        while (nheld < AIC_DMA_CH_NUM)
        {
            held[nheld] = hal_request_dma_chan();
            if (!held[nheld])
            {
                break;
            }
            nheld++;
        }
        if (nheld != AIC_DMA_CH_NUM)
        {
            rt_kprintf("uart_dma_test: held only %u channels\n",
                       (rt_uint32_t)nheld);
        }
    }

    rt_memset(&w, 0, sizeof(w));
    w.dev = dev;
    w.buf = tx;
    w.total = total;
    w.chunk = chunk;

    tid = rt_thread_create("uartdmatx", aic_uart_dma_writer_entry, &w,
                           1024, 20, 10);
    if (!tid)
    {
        ret = -RT_ENOMEM;
        goto out_release;
    }
    rt_thread_startup(tid);

    start = rt_tick_get_millisecond();
    deadline = rt_tick_get() +
               rt_tick_from_millisecond(UART_DMA_TEST_TIMEOUT_MS);

    while (got < total && rt_tick_get() < deadline)
    {
        int c = aic_uart_getc(&uart->serial);

        if (c < 0)
        {
            rt_thread_mdelay(1);
        }
        else
        {
            rx[got++] = (rt_uint8_t)c;
        }
    }

    while (!w.done && rt_tick_get() < deadline)
    {
        rt_thread_mdelay(1);
    }
    if (!w.done)
    {
        rt_tick_t stop_deadline;

        /* Ask the writer to stop and wait for it to leave the buffers.
         * A dynamic thread whose entry returns is queued as defunct and
         * freed by the idle thread, so the handle must not be passed to
         * rt_thread_delete() here: it may already be gone. */
        w.stop = RT_TRUE;
        stop_deadline = rt_tick_get() + rt_tick_from_millisecond(2000);
        while (!w.done && rt_tick_get() < stop_deadline)
        {
            rt_thread_mdelay(1);
        }
        if (!w.done)
        {
            rt_kprintf("uart_dma_test: writer stuck\n");
            /* It still owns tx/rx; leaking them is safer than freeing. */
            return -RT_ETIMEOUT;
        }
        rt_kprintf("uart_dma_test: writer stopped early\n");
        ret = -RT_ETIMEOUT;
        goto out_release;
    }

    while (uart->dma_tx_busy && rt_tick_get() < deadline)
    {
        rt_thread_mdelay(1);
    }
    if (uart->dma_tx_busy)
    {
        rt_kprintf("uart_dma_test: completion timeout\n");
        ret = -RT_ETIMEOUT;
        goto out_release;
    }

    while (!(uart_read(uart, UART_LSR) & UART_LSR_TEMT) &&
           rt_tick_get() < deadline)
    {
        rt_thread_mdelay(1);
    }

    if (w.err)
    {
        ret = w.err;
        goto out_release;
    }
    if (got != total)
    {
        rt_kprintf("uart_dma_test: stream received %u of %u bytes\n",
                   (rt_uint32_t)got, total);
        ret = -RT_ETIMEOUT;
        goto out_release;
    }

    for (i = 0; i < total; i++)
    {
        if (tx[i] != rx[i])
        {
            rt_kprintf("uart_dma_test: stream mismatch at %u "
                       "(tx 0x%02x rx 0x%02x)\n", i, tx[i], rx[i]);
            same = RT_FALSE;
            break;
        }
    }
    if (!same)
    {
        ret = -RT_EIO;
    }

    if (kbps)
    {
        rt_uint32_t elapsed = rt_tick_get_millisecond() - start;

        *kbps = elapsed ? (rt_uint32_t)(((rt_uint64_t)total * 1000) /
                                        (elapsed * 1024)) : 0;
    }

out_release:
    while (nheld > 0)
    {
        nheld--;
        hal_release_dma_chan(held[nheld]);
    }
    aic_uart_dma_test_finish(uart);
out_close:
    if (tx)
    {
        aic_dma_free(tx);
    }
    if (rx)
    {
        aic_dma_free(rx);
    }
    rt_device_close(dev);

    return ret;
}

/*
 * TX DMA with the standard INT_RX path active at the same time.  The
 * receive side goes through the 1 ms RX poll thread and the serial ring,
 * exactly like an application using rt_device_read(); this also proves
 * the TX path no longer resets the RX FIFO mid-transfer.
 */
rt_err_t aic_uart1_dma_mixed_rx_test(rt_uint32_t len, rt_uint32_t *kbps)
{
    struct aic_uart *uart = &aic_uart1;
    rt_device_t dev;
    rt_uint8_t *tx = RT_NULL;
    rt_uint8_t *rx = RT_NULL;
    rt_uint8_t scratch[RT_SERIAL_RB_BUFSZ];
    rt_size_t got = 0;
    rt_tick_t start, deadline;
    rt_uint32_t i;
    rt_err_t ret = RT_EOK;

    if (len < UART_DMA_TEST_MIN_BYTES || len > UART_DMA_TEST_MAX_BYTES)
    {
        return -RT_EINVAL;
    }

    dev = rt_device_find("uart1");
    if (!dev)
    {
        return -RT_ENOSYS;
    }

    aic_uart_dma_tx_abort(uart);
    ret = rt_device_open(dev, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX |
                              RT_DEVICE_FLAG_DMA_TX);
    if (ret != RT_EOK)
    {
        return ret;
    }

    tx = aic_dma_alloc(len);
    rx = aic_dma_alloc(len);
    if (!tx || !rx)
    {
        ret = -RT_ENOMEM;
        goto out_close;
    }

    for (i = 0; i < len; i++)
    {
        tx[i] = (rt_uint8_t)(i * 11 + 0x17);
    }
    rt_memset(rx, 0, len);

    /* Loopback first, then let the RX thread move any byte that was in
     * flight before the switch, and empty both the FIFO and the ring. */
    uart_write(uart, UART_MCR,
               uart_read(uart, UART_MCR) | UART_MCR_LOOPBACK);
    rt_thread_mdelay(2);
    while (aic_uart_getc(&uart->serial) >= 0)
    {
    }
    while (rt_device_read(dev, 0, scratch, sizeof(scratch)) > 0)
    {
    }

    start = rt_tick_get_millisecond();
    deadline = rt_tick_get() +
               rt_tick_from_millisecond(UART_DMA_TEST_TIMEOUT_MS);

    if (rt_device_write(dev, 0, tx, len) != (rt_ssize_t)len)
    {
        ret = -RT_EIO;
        goto out;
    }

    while (got < len && rt_tick_get() < deadline)
    {
        rt_size_t n = rt_device_read(dev, 0, rx + got, len - got);

        if (n == 0)
        {
            rt_thread_mdelay(1);
        }
        else
        {
            got += n;
        }
    }
    if (got != len)
    {
        rt_kprintf("uart_dma_test: mixed rx got %u of %u bytes\n",
                   (rt_uint32_t)got, len);
        ret = -RT_ETIMEOUT;
        goto out;
    }

    while (uart->dma_tx_busy && rt_tick_get() < deadline)
    {
        rt_thread_mdelay(1);
    }
    if (uart->dma_tx_busy)
    {
        ret = -RT_ETIMEOUT;
        goto out;
    }

    while (!(uart_read(uart, UART_LSR) & UART_LSR_TEMT) &&
           rt_tick_get() < deadline)
    {
        rt_thread_mdelay(1);
    }

    for (i = 0; i < len; i++)
    {
        if (tx[i] != rx[i])
        {
            rt_kprintf("uart_dma_test: mixed rx mismatch at %u "
                       "(tx 0x%02x rx 0x%02x)\n", i, tx[i], rx[i]);
            ret = -RT_EIO;
            goto out;
        }
    }

    if (kbps)
    {
        rt_uint32_t elapsed = rt_tick_get_millisecond() - start;

        *kbps = elapsed ? (rt_uint32_t)(((rt_uint64_t)len * 1000) /
                                        (elapsed * 1024)) : 0;
    }

out:
    aic_uart_dma_test_finish(uart);
out_close:
    if (tx)
    {
        aic_dma_free(tx);
    }
    if (rx)
    {
        aic_dma_free(rx);
    }
    rt_device_close(dev);

    return ret;
}

/*
 * Close the device while a DMA transfer is in flight, then run a fresh
 * transfer on a reopened device.  Guards the abort path and proves the
 * channel and data queue are released cleanly.
 */
rt_err_t aic_uart1_dma_close_test(void)
{
    struct aic_uart *uart = &aic_uart1;
    rt_device_t dev;
    rt_uint8_t *tx;
    rt_err_t ret;

    dev = rt_device_find("uart1");
    if (!dev)
    {
        return -RT_ENOSYS;
    }

    aic_uart_dma_tx_abort(uart);
    ret = rt_device_open(dev, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_DMA_TX);
    if (ret != RT_EOK)
    {
        return ret;
    }

    tx = aic_dma_alloc(2048);
    if (!tx)
    {
        rt_device_close(dev);
        return -RT_ENOMEM;
    }
    rt_memset(tx, 0xa5, 2048);
    aic_uart_dma_test_prepare(uart);

    if (rt_device_write(dev, 0, tx, 2048) != 2048)
    {
        ret = -RT_EIO;
    }
    /* Close with the first piece still running: must abandon it. */
    rt_device_close(dev);
    aic_dma_free(tx);

    if (uart->dma_tx_busy || uart->dma_tx_chan)
    {
        rt_kprintf("uart_dma_test: close left a live channel\n");
        return -RT_EBUSY;
    }

    aic_uart_dma_test_finish(uart);
    rt_thread_mdelay(5);

    return aic_uart1_dma_loopback_test(64, RT_NULL);
}

#ifdef RT_USING_FINSH
#include <finsh.h>
#include <stdlib.h>

static void uart_dma_test(int argc, char **argv)
{
    rt_uint32_t len = 1024;
    rt_uint32_t kbps = 0;
    rt_err_t ret;

    if (argc >= 2)
    {
        len = 0;
        for (int i = 0; argv[1][i] >= '0' && argv[1][i] <= '9'; i++)
        {
            len = len * 10 + (rt_uint32_t)(argv[1][i] - '0');
        }
    }

    ret = aic_uart1_dma_loopback_test(len, &kbps);
    if (ret == RT_EOK)
    {
        rt_kprintf("uart_dma_test: %u bytes PASS (%u KB/s)\n", len, kbps);
    }
    else
    {
        rt_kprintf("uart_dma_test: FAIL (%d)\n", ret);
    }
}
MSH_CMD_EXPORT(uart_dma_test, run a UART1 DMA loopback test);

static void uart_dma_stream(int argc, char **argv)
{
    rt_uint32_t total = 4096;
    rt_uint32_t chunk = 513;
    rt_uint32_t kbps = 0;
    rt_err_t ret;

    if (argc >= 2)
    {
        total = (rt_uint32_t)strtoul(argv[1], RT_NULL, 0);
    }
    if (argc >= 3)
    {
        chunk = (rt_uint32_t)strtoul(argv[2], RT_NULL, 0);
    }

    ret = aic_uart1_dma_loopback_stream(total, chunk, RT_FALSE, &kbps);
    if (ret == RT_EOK)
    {
        rt_kprintf("uart_dma_stream: %u bytes in %u-byte chunks PASS "
                   "(%u KB/s)\n", total, chunk, kbps);
    }
    else
    {
        rt_kprintf("uart_dma_stream: FAIL (%d)\n", ret);
    }
}
MSH_CMD_EXPORT(uart_dma_stream, queue UART1 DMA chunks back to back);

static void uart_dma_close(int argc, char **argv)
{
    rt_err_t ret;

    (void)argc;
    (void)argv;
    ret = aic_uart1_dma_close_test();
    if (ret == RT_EOK)
    {
        rt_kprintf("uart_dma_close: PASS\n");
    }
    else
    {
        rt_kprintf("uart_dma_close: FAIL (%d)\n", ret);
    }
}
MSH_CMD_EXPORT(uart_dma_close, close uart1 during a DMA transfer);
#endif /* RT_USING_FINSH */
#endif /* BSP_USING_UART1 && BSP_USING_UART_DMA */

#ifdef BSP_USING_UART_DMA
static void aic_uart_dma_check(struct aic_uart *uart)
{
    rt_uint32_t now;

    if (!uart->dma_tx_busy || uart->serial.serial_tx == RT_NULL)
    {
        return;
    }

    now = rt_tick_get_millisecond();
    if ((rt_int32_t)(now - uart->dma_tx_deadline) < 0)
    {
        return;
    }

    rt_kprintf("uart%d: DMA TX stalled, aborting\n", uart->id);
    if (aic_uart_dma_tx_abort(uart) && uart->serial.serial_tx != RT_NULL)
    {
        /*
         * The aborted chunk is still the oldest entry of the serial DMA
         * queue.  Pop it so the framework can accept and start the next
         * write instead of staying "activated" forever.
         */
        rt_hw_serial_isr(&uart->serial, RT_SERIAL_EVENT_TX_DMADONE);
    }
}

static void aic_uart_dma_watchdog_entry(void *param)
{
    (void)param;

    for (;;)
    {
#ifdef BSP_USING_UART0
        aic_uart_dma_check(&aic_uart0);
#endif
#ifdef BSP_USING_UART1
        aic_uart_dma_check(&aic_uart1);
#endif
#ifdef BSP_USING_UART2
        aic_uart_dma_check(&aic_uart2);
#endif
#ifdef BSP_USING_UART3
        aic_uart_dma_check(&aic_uart3);
#endif
#ifdef BSP_USING_UART4
        aic_uart_dma_check(&aic_uart4);
#endif
#ifdef BSP_USING_UART5
        aic_uart_dma_check(&aic_uart5);
#endif
#ifdef BSP_USING_UART6
        aic_uart_dma_check(&aic_uart6);
#endif
#ifdef BSP_USING_UART7
        aic_uart_dma_check(&aic_uart7);
#endif
        rt_thread_mdelay(100);
    }
}

/*
 * A thread, not a timer: recovery stops the channel and touches the
 * serial framework, which must not run in the tick ISR (this BSP does
 * not enable RT_USING_TIMER_SOFT).
 */
static void aic_uart_dma_watchdog_start(void)
{
    rt_thread_t tid;

    tid = rt_thread_create("uartdma", aic_uart_dma_watchdog_entry, RT_NULL,
                           1536, 30, 10);
    if (tid)
    {
        rt_thread_startup(tid);
    }
}
#endif /* BSP_USING_UART_DMA */

void rt_hw_serial_init(void)
{
#ifdef BSP_USING_UART0
    aic_uart_register(&aic_uart0, "uart0");
#endif
#ifdef BSP_USING_UART1
    aic_uart_register(&aic_uart1, "uart1");
#endif
#ifdef BSP_USING_UART2
    aic_uart_register(&aic_uart2, "uart2");
#endif
#ifdef BSP_USING_UART3
    aic_uart_register(&aic_uart3, "uart3");
#endif
#ifdef BSP_USING_UART4
    aic_uart_register(&aic_uart4, "uart4");
#endif
#ifdef BSP_USING_UART5
    aic_uart_register(&aic_uart5, "uart5");
#endif
#ifdef BSP_USING_UART6
    aic_uart_register(&aic_uart6, "uart6");
#endif
#ifdef BSP_USING_UART7
    aic_uart_register(&aic_uart7, "uart7");
#endif

#ifdef BSP_USING_UART_DMA
    aic_uart_dma_watchdog_start();
#endif
}
