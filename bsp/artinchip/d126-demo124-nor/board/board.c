/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D126 (D12p) board initialization.
 */

#include <rthw.h>
#include <rtthread.h>
#include <rtdevice.h>
#include <isa_ext.h>
#include <riscv_timer.h>

#include "board.h"

extern char __heap_start;
extern char __heap_end;

void rt_hw_board_init(void)
{
    riscv_isa_ext_set(I);
    riscv_isa_ext_set(M);
    riscv_isa_ext_set(A);
    riscv_isa_ext_set(F);
    riscv_isa_ext_set(C);

    aic_board_clock_init();

    rt_hw_interrupt_init();
    riscv_timer_set_frequency(aic_cpu_clock());
    rt_hw_tick_init();
    /* CLIC requires CLICINTIE[MTIP] in addition to mie.MTIE. */
    rt_hw_interrupt_umask(7);

    rt_hw_uart_init();
    rt_console_set_device("uart0");

#ifdef RT_USING_HEAP
    rt_system_heap_init((void *)&__heap_start, (void *)&__heap_end);
#endif

    rt_kprintf("\nArtInChip D126 (D12p) RT-Thread BSP\n");
    rt_kprintf("CPU: T-Head E907F (RV32IMafc), %lu Hz\n", aic_cpu_clock());
    rt_kprintf("UART0: %lu Hz, %d baud\n", aic_uart_clock(), BSP_UART0_BAUD);
    rt_kprintf("PSRAM: 0x%08x, %u MB\n", (unsigned)AIC_PSRAM_BASE,
               (unsigned)(AIC_PSRAM_SIZE / (1024 * 1024)));
    {
        rt_uint32_t info = *(volatile rt_uint32_t *)(uintptr_t)
            (AIC_CLIC_BASE + 0x0004UL);
        rt_kprintf("CLIC: %u IRQs, %u control bits\n",
                   (unsigned)(info & 0x1fff),
                   (unsigned)((info >> 21) & 0xf));
    }

#ifdef RT_USING_COMPONENTS_INIT
    rt_components_board_init();
#endif

#ifdef RT_USING_CONSOLE
    rt_kprintf("console: UART0 char device\n");
#endif
}
