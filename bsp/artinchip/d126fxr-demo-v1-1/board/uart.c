/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * D12p UART0 console and RT-Thread serial device.
 *
 * RX runs through the standard rt_serial_device interrupt path: the
 * D12p UART0 interrupt (IRQ 78) drains the hardware FIFO into the
 * framework ring buffer and FinSH is woken through rx_indicate.
 * Console output stays polled so rt_kprintf also works before the
 * device framework is ready and from panic paths.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>
#include <stdint.h>

#include "board.h"
#include "aic_uart_reg.h"

/*
 * Interrupt bits from Luban-Lite
 * bsp/artinchip/include/hal/aic_hal_uart.h (DesignWare UART).
 */
#define AIC_UART_IER_RDI            0x01
#define AIC_UART_IER_THRI           0x02
#define AIC_UART_IIR_MASK           0x0f
#define AIC_UART_IIR_THR_EMPTY      0x02
#define AIC_UART_IIR_RECV_DATA      0x04
#define AIC_UART_IIR_RECV_LINE      0x06
#define AIC_UART_IIR_CHAR_TIMEOUT   0x0c

static inline uint32_t uart_readl(uint32_t reg)
{
    return *(volatile uint32_t *)(uintptr_t)(AIC_UART0_BASE + reg);
}

static inline void uart_writel(uint32_t value, uint32_t reg)
{
    *(volatile uint32_t *)(uintptr_t)(AIC_UART0_BASE + reg) = value;
}

static struct rt_serial_device aic_uart0;

static void uart_putc_raw(char ch)
{
    while (!(uart_readl(UART_LSR) & UART_LSR_THRE));
    uart_writel((uint8_t)ch, UART_THR);
}

/* Console output keeps the kernel's \n -> \r\n behaviour. */
static void uart_putc(char ch)
{
    if (ch == '\n')
    {
        uart_putc_raw('\r');
    }

    uart_putc_raw(ch);
}

static int uart_getchar_polled(void)
{
    if (!(uart_readl(UART_LSR) & UART_LSR_DR))
    {
        return -1;
    }

    return (int)(uart_readl(UART_RBR) & 0xff);
}

static rt_err_t aic_uart_configure(struct rt_serial_device *serial,
                                   struct serial_configure *cfg)
{
    unsigned long uart_clk;
    uint32_t divisor;

    RT_ASSERT(serial != RT_NULL);
    RT_ASSERT(cfg != RT_NULL);

    if (cfg->baud_rate == 0)
    {
        return -RT_EINVAL;
    }
    if (cfg->data_bits != DATA_BITS_8 ||
        cfg->stop_bits != STOP_BITS_1 ||
        cfg->parity != PARITY_NONE ||
        cfg->bit_order != BIT_ORDER_LSB ||
        cfg->invert != NRZ_NORMAL)
    {
        return -RT_EINVAL;
    }

    uart_clk = aic_uart_clock();
    divisor = (uint32_t)((uart_clk + (8UL * cfg->baud_rate)) /
                         (16UL * cfg->baud_rate));
    if (divisor == 0)
    {
        divisor = 1;
    }

    /* The divisor latch cannot be written while the UART is busy. */
    while (uart_readl(UART_USR) & UART_USR_BUSY);

    uart_writel(UART_LCR_DLAB | UART_LCR_8N1, UART_LCR);
    uart_writel(divisor & 0xff, UART_DLL);
    uart_writel((divisor >> 8) & 0xff, UART_DLH);
    uart_writel(UART_LCR_8N1, UART_LCR);

    return RT_EOK;
}

static rt_err_t aic_uart_control(struct rt_serial_device *serial, int cmd,
                                 void *arg)
{
    (void)serial;
    (void)arg;

    switch (cmd)
    {
    case RT_DEVICE_CTRL_CLR_INT:
        uart_writel(uart_readl(UART_IER) & ~AIC_UART_IER_RDI, UART_IER);
        rt_hw_interrupt_mask(AIC_UART0_IRQ);
        break;

    case RT_DEVICE_CTRL_SET_INT:
        /*
         * Drop bytes buffered before the device was opened, then let
         * the framework (which has allocated its RX FIFO by now) take
         * over.
         */
        while (uart_getchar_polled() >= 0);
        uart_writel(uart_readl(UART_IER) | AIC_UART_IER_RDI, UART_IER);
        rt_hw_interrupt_umask(AIC_UART0_IRQ);
        break;

    default:
        break;
    }

    return RT_EOK;
}

static int aic_uart_getc(struct rt_serial_device *serial)
{
    (void)serial;
    return uart_getchar_polled();
}

static int aic_uart_putc(struct rt_serial_device *serial, char c)
{
    (void)serial;
    /* _serial_poll_tx inserts '\r' for the stream flag itself. */
    uart_putc_raw(c);
    return 1;
}

static const struct rt_uart_ops aic_uart_ops =
{
    .configure = aic_uart_configure,
    .control = aic_uart_control,
    .putc = aic_uart_putc,
    .getc = aic_uart_getc,
    .dma_transmit = RT_NULL,
};

static void aic_uart0_isr(int irq, void *param)
{
    uint32_t status;

    (void)irq;
    (void)param;

    /* Reading IIR acknowledges the interrupt. */
    status = uart_readl(UART_IIR) & AIC_UART_IIR_MASK;
    switch (status)
    {
    case AIC_UART_IIR_RECV_DATA:
    case AIC_UART_IIR_CHAR_TIMEOUT:
        rt_hw_serial_isr(&aic_uart0, RT_SERIAL_EVENT_RX_IND);
        break;

    case AIC_UART_IIR_RECV_LINE:
        /* Reading LSR clears the line-status condition. */
        (void)uart_readl(UART_LSR);
        rt_hw_serial_isr(&aic_uart0, RT_SERIAL_EVENT_RX_IND);
        break;

    case AIC_UART_IIR_THR_EMPTY:
        /* TX is polled; never leave the THRE interrupt enabled. */
        uart_writel(uart_readl(UART_IER) & ~AIC_UART_IER_THRI, UART_IER);
        break;

    default:
        (void)uart_readl(UART_LSR);
        break;
    }
}

void rt_hw_uart_init(void)
{
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;

    /* PA0 = UART0_TX, PA1 = UART0_RX (vendor PBP configuration). */
    *(volatile uint32_t *)(uintptr_t)AIC_PA0_PINMUX_REG = AIC_UART0_PINMUX_VAL;
    *(volatile uint32_t *)(uintptr_t)AIC_PA1_PINMUX_REG = AIC_UART0_PINMUX_VAL;

    uart_writel(0, UART_IER);
    /* TX trigger is irrelevant to the polled console; keep it at 0. */
    uart_writel(UART_FCR_ENABLE | UART_FCR_RX_RESET | UART_FCR_TX_RESET |
                UART_FCR_RX_TRIG(3) | UART_FCR_TX_TRIG(0), UART_FCR);

    config.baud_rate = BSP_UART0_BAUD;
    config.bufsz = RT_SERIAL_RB_BUFSZ;
    aic_uart0.ops = &aic_uart_ops;
    aic_uart0.config = config;

    (void)aic_uart_configure(&aic_uart0, &config);

    rt_hw_interrupt_install(AIC_UART0_IRQ, aic_uart0_isr, RT_NULL, "uart0");
    rt_hw_serial_register(&aic_uart0, "uart0",
                          RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX,
                          RT_NULL);
}

void rt_hw_console_output(const char *str)
{
    if (str == RT_NULL)
    {
        return;
    }

    while (*str)
    {
        uart_putc(*str);
        str++;
    }
}

signed char rt_hw_console_getchar(void)
{
    return (signed char)uart_getchar_polled();
}
