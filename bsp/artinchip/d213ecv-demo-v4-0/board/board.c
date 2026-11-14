/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D213ECV-DEMO-V4-0 board initialization.
 */

#include <rthw.h>
#include <rtthread.h>
#include <rtdevice.h>
#include <isa_ext.h>
#include <riscv_timer.h>
#include <stdint.h>

#include "board.h"

extern char __heap_start;
extern char __heap_end;

extern void rt_hw_serial_init(void);
extern int rt_hw_tick_init(void);

static void aic_cpu_init(void)
{
    uint64_t value;

    /* Enable the FPU (FS = Initial). */
    __asm__ volatile("csrr %0, mstatus" : "=r"(value));
    value &= ~(3UL << 13);
    value |= (1UL << 13);
    __asm__ volatile("csrw mstatus, %0" : : "r"(value));

    /*
     * C906: enable THEADISAEE and misaligned memory access.  These bits
     * are also set by the vendor SystemInit(); doing it here keeps the
     * BSP independent from the vendor C library.
     */
    __asm__ volatile("csrr %0, 0x7c0" : "=r"(value));
    value |= (1UL << 22) | (1UL << 15);
    __asm__ volatile("csrw 0x7c0, %0" : : "r"(value));
}

void rt_hw_board_init(void)
{
    riscv_isa_ext_set(I);
    riscv_isa_ext_set(M);
    riscv_isa_ext_set(A);
    riscv_isa_ext_set(F);
    riscv_isa_ext_set(D);
    riscv_isa_ext_set(C);

    aic_cpu_init();
    riscv_timer_set_frequency(BSP_CLINT_FREQ);

    /* Peripheral gates and UART0 pinmux.  The CPU/PLL tree is set up by
     * the ArtInChip bootloader before this image is entered. */
    aic_board_sysclk_init();
    aic_board_pinmux_init();

#ifdef RT_USING_HEAP
    rt_system_heap_init((void *)&__heap_start, (void *)&__heap_end);
#endif

    aic_plic_quirk_init();
    rt_hw_interrupt_init();

    rt_hw_tick_init();

    rt_hw_serial_init();

#ifdef RT_USING_COMPONENTS_INIT
    rt_components_board_init();
#endif

#if defined(RT_USING_CONSOLE) && defined(RT_USING_DEVICE) && \
    (defined(BSP_USING_UART0) || defined(BSP_USING_UART1) || \
     defined(BSP_USING_UART2) || defined(BSP_USING_UART3) || \
     defined(BSP_USING_UART4) || defined(BSP_USING_UART5) || \
     defined(BSP_USING_UART6) || defined(BSP_USING_UART7))
    rt_console_set_device(RT_CONSOLE_DEVICE_NAME);
#endif
}
