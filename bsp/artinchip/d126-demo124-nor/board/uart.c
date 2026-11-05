/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * D12p UART0 polled console.
 *
 * This is intentionally a minimal bring-up console, not the RT-Thread
 * serial device driver.  It lets the kernel and FinSH run before the
 * UART device framework is ported.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <stdint.h>

#include "board.h"

#define UART_RBR            0x00
#define UART_THR            0x00
#define UART_DLL            0x00
#define UART_DLH            0x04
#define UART_IER            0x04
#define UART_FCR            0x08
#define UART_LCR            0x0c
#define UART_LSR            0x14

#define UART_LCR_DLAB       0x80
#define UART_LCR_8N1        0x03
#define UART_FCR_ENABLE     0x01
#define UART_FCR_CLEAR_RX   0x02
#define UART_FCR_CLEAR_TX   0x04
#define UART_FCR_TRIGGER    0xc0
#define UART_LSR_DR         0x01
#define UART_LSR_THRE       0x20

static inline uint32_t uart_readl(uint32_t reg)
{
    return *(volatile uint32_t *)(uintptr_t)(AIC_UART0_BASE + reg);
}

static inline void uart_writel(uint32_t value, uint32_t reg)
{
    *(volatile uint32_t *)(uintptr_t)(AIC_UART0_BASE + reg) = value;
}

signed char rt_hw_console_getchar(void);

static void uart_putc(char ch)
{
    if (ch == '\n')
    {
        while (!(uart_readl(UART_LSR) & UART_LSR_THRE));
        uart_writel('\r', UART_THR);
    }

    while (!(uart_readl(UART_LSR) & UART_LSR_THRE));
    uart_writel((uint8_t)ch, UART_THR);
}

static rt_err_t aic_console_init(rt_device_t dev)
{
    (void)dev;
    return RT_EOK;
}

static rt_err_t aic_console_open(rt_device_t dev, rt_uint16_t oflag)
{
    (void)dev;
    (void)oflag;
    return RT_EOK;
}

static rt_err_t aic_console_close(rt_device_t dev)
{
    (void)dev;
    return RT_EOK;
}

static rt_ssize_t aic_console_read(rt_device_t dev, rt_off_t pos,
                                   void *buffer, rt_size_t size)
{
    signed char ch;
    rt_uint8_t *out = (rt_uint8_t *)buffer;

    (void)dev;
    (void)pos;

    if (buffer == RT_NULL || size == 0)
    {
        return 0;
    }

    do
    {
        ch = rt_hw_console_getchar();
        if (ch < 0)
        {
            rt_thread_mdelay(1);
        }
    } while (ch < 0);

    out[0] = (rt_uint8_t)ch;
    return 1;
}

static rt_ssize_t aic_console_write(rt_device_t dev, rt_off_t pos,
                                    const void *buffer, rt_size_t size)
{
    const char *str = (const char *)buffer;
    rt_size_t i;

    (void)dev;
    (void)pos;

    if (buffer == RT_NULL)
    {
        return 0;
    }

    for (i = 0; i < size; i++)
    {
        uart_putc(str[i]);
    }

    return (rt_ssize_t)size;
}

static rt_err_t aic_console_control(rt_device_t dev, int cmd, void *args)
{
    (void)dev;
    (void)cmd;
    (void)args;
    return RT_EOK;
}

static struct rt_device aic_console_device =
{
    .type = RT_Device_Class_Char,
    .init = aic_console_init,
    .open = aic_console_open,
    .close = aic_console_close,
    .read = aic_console_read,
    .write = aic_console_write,
    .control = aic_console_control,
};

void rt_hw_uart_init(void)
{
    unsigned long uart_clk;
    uint32_t divisor;

    /* PA0 = UART0_TX, PA1 = UART0_RX (vendor PBP configuration). */
    *(volatile uint32_t *)(uintptr_t)AIC_PA0_PINMUX_REG = AIC_UART0_PINMUX_VAL;
    *(volatile uint32_t *)(uintptr_t)AIC_PA1_PINMUX_REG = AIC_UART0_PINMUX_VAL;

    uart_clk = aic_uart_clock();
    divisor = (uint32_t)((uart_clk + (8UL * BSP_UART0_BAUD)) /
                         (16UL * BSP_UART0_BAUD));
    if (divisor == 0)
    {
        divisor = 1;
    }

    uart_writel(0, UART_IER);
    uart_writel(UART_LCR_DLAB | UART_LCR_8N1, UART_LCR);
    uart_writel(divisor & 0xff, UART_DLL);
    uart_writel((divisor >> 8) & 0xff, UART_DLH);
    uart_writel(UART_LCR_8N1, UART_LCR);
    uart_writel(UART_FCR_ENABLE | UART_FCR_CLEAR_RX |
                UART_FCR_CLEAR_TX | UART_FCR_TRIGGER, UART_FCR);

    rt_device_register(&aic_console_device, "uart0",
                       RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_STREAM);
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
    if (!(uart_readl(UART_LSR) & UART_LSR_DR))
    {
        return -1;
    }

    return (signed char)(uart_readl(UART_RBR) & 0xff);
}
