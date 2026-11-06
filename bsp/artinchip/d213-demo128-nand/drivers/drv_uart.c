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

#include "board.h"
#include "aic_clk_id.h"

#define UART_RBR   0x00
#define UART_THR   0x00
#define UART_DLL   0x00
#define UART_DLH   0x04
#define UART_IER   0x04
#define UART_IIR   0x08
#define UART_FCR   0x08
#define UART_LCR   0x0C
#define UART_MCR   0x10
#define UART_LSR   0x14
#define UART_USR   0x7C
#define UART_RXCTL 0xA0

#define UART_LSR_DR   0x01
#define UART_LSR_OE   0x02
#define UART_LSR_PE   0x04
#define UART_LSR_FE   0x08
#define UART_LSR_BI   0x10
#define UART_LSR_THRE 0x20
#define UART_LSR_TEMT 0x40

#define UART_FCR_ENABLE   0x01
#define UART_FCR_RX_RESET 0x02
#define UART_FCR_TX_RESET 0x04
/* RX trigger 0 = one byte; do not rely on the character-timeout path. */
#define UART_FCR_RX_TRIG  (0U << 6)
#define UART_FCR_TX_TRIG  (3U << 4)
#define UART_LCR_DLAB     0x80
#define UART_LCR_STOP     0x04
#define UART_LCR_PARITY   0x08
#define UART_LCR_EVEN     0x10
#define UART_LCR_8N1      0x03
#define UART_RXCTL_ENABLE 0x01

struct aic_uart
{
    uint32_t id;
    uint32_t base;
    uint32_t irq;
    uint32_t clk;
    rt_bool_t rx_thread_started;
    const char *name;
    struct rt_serial_device serial;
};

#define AIC_UART_INSTANCE(n)                                             \
    {                                                                    \
        .id = (n),                                                       \
        .base = AIC_UART_BASE(n),                                        \
        .irq = AIC_UART_IRQ(n),                                          \
        .clk = AIC_UART_CLK_FREQ,                                        \
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
static struct aic_uart aic_uart7 = AIC_UART_INSTANCE(7);
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

    /* RS-232 mode, no modem flow control. */
    uart_write(uart, UART_MCR, 0);

    /* Enable and reset FIFOs, one-byte RX trigger. */
    uart_write(uart, UART_FCR, UART_FCR_ENABLE | UART_FCR_RX_RESET |
                               UART_FCR_TX_RESET | UART_FCR_RX_TRIG |
                               UART_FCR_TX_TRIG);

    /* Enable the receiver. */
    uart_write(uart, UART_RXCTL, UART_RXCTL_ENABLE);

    return RT_EOK;
}

static void aic_uart_start_rx_poll(struct aic_uart *uart);

static rt_err_t aic_uart_control(struct rt_serial_device *serial,
                                 int cmd, void *arg)
{
    struct aic_uart *uart = (struct aic_uart *)serial->parent.user_data;

    switch (cmd)
    {
    case RT_DEVICE_CTRL_CLR_INT:
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

static const struct rt_uart_ops aic_uart_ops = {
    .configure = aic_uart_configure,
    .control = aic_uart_control,
    .putc = aic_uart_putc,
    .getc = aic_uart_getc,
    .dma_transmit = RT_NULL,
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
                          RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX,
                          uart);

    /*
     * Keep the controller and PLIC interrupt lines disabled and feed the
     * serial framework from the polling thread instead.  The poller is
     * started from RT_DEVICE_CTRL_SET_INT, after the scheduler is running.
     */
    aic_uart_write_ier(uart, 0);
    rt_hw_interrupt_mask(uart->irq);
}

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
}
