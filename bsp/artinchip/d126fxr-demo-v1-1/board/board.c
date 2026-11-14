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

/*
 * E907 extended exception status (MEXSTATUS, CSR 0x7e1).  Bit 16 makes
 * the core speculatively execute IPUSH on interrupt entry, bit 17 swaps
 * sp/mscratch automatically for a level-0 interrupt.  The ArtInChip
 * bootloader runs the vendor SystemInit(), which enables both, and the
 * bits survive the jump into the application.  The RT-Thread common
 * port switches to its own IRQ stack in software through mscratch, so
 * SPSWAPEN makes the hardware swap a second time on top of the vector's
 * csrrw: the exception frame is built with mscratch (the IRQ stack top)
 * as the interrupted sp and is then overwritten by the C trap handler.
 */
#define D12P_MEXSTATUS              0x7e1UL
#define D12P_MEXSTATUS_SPUSHEN      (1UL << 16)
#define D12P_MEXSTATUS_SPSWAPEN     (1UL << 17)

#define D12P_MCPUID                 0xfc0UL
#define D12P_EFUSE_CMU_REG          0x18020904UL
#define D12P_EFUSE_218_REG          0x19010218UL
#define D12P_EFUSE_21C_REG          0x1901021cUL

extern char __heap_start;
extern char __heap_end;

/*
 * The value the bootloader left in MEXSTATUS.  It is captured before the
 * common startup clears .bss, so a non-zero initializer keeps the storage
 * in .data.
 */
static rt_ubase_t g_mexstatus_boot = ~0UL;

void riscv_early_setup(void)
{
    unsigned long mexstatus;

    __asm__ volatile("csrr %0, 0x7e1" : "=r"(mexstatus));
    g_mexstatus_boot = mexstatus;

    mexstatus &= ~(D12P_MEXSTATUS_SPUSHEN | D12P_MEXSTATUS_SPSWAPEN);
    __asm__ volatile("csrw 0x7e1, %0" : : "r"(mexstatus));
}

/*
 * The eFuse mark_id/psram_id select the PSRAM part in the PBP.  The
 * mapping below is the D12p table in Luban-Lite
 * bsp/artinchip/sys/d12p/ram_param.c.
 */
static unsigned int d12p_psram_size_mb(unsigned int mark_id)
{
    switch (mark_id)
    {
    case 0x01:                  /* D125CBS */
    case 0x05:                  /* D126FBV */
    case 0x07:                  /* D126FBR */
        return 8;
    case 0x02:                  /* D125CCS */
    case 0x06:                  /* D126FCR */
        return 16;
    case 0x03:                  /* D125DDS */
    case 0x04:                  /* D125EDS */
        return 32;
    case 0x21:                  /* G720ADU */
    case 0x22:                  /* G720AFU */
    case 0x23:                  /* G720BGU */
        return 4;
    default:
        return 0;
    }
}

static void d12p_show_hardware_info(void)
{
    unsigned long id0, id1, id2;
    unsigned long mexstatus;
    unsigned int mark_id, psram_id, mb;

    /* MCPUID returns three consecutive identification words. */
    __asm__ volatile("csrr %0, 0xfc0\n\t"
                     "csrr %1, 0xfc0\n\t"
                     "csrr %2, 0xfc0"
                     : "=r"(id0), "=r"(id1), "=r"(id2));

    __asm__ volatile("csrr %0, 0x7e1" : "=r"(mexstatus));

    *(volatile rt_uint32_t *)(uintptr_t)D12P_EFUSE_CMU_REG = 0x1100;
    mark_id = *(volatile rt_uint32_t *)(uintptr_t)D12P_EFUSE_218_REG & 0xff;
    psram_id = (*(volatile rt_uint32_t *)(uintptr_t)D12P_EFUSE_21C_REG & 0xff) >> 4;
    *(volatile rt_uint32_t *)(uintptr_t)D12P_EFUSE_CMU_REG = 0;

    mb = d12p_psram_size_mb(mark_id);

    rt_kprintf("MCPUID: 0x%08lx 0x%08lx 0x%08lx\n", id0, id1, id2);
    rt_kprintf("eFuse: mark_id=0x%02x psram_id=0x%02x, PSRAM %u MB\n",
               mark_id, psram_id, mb);
    if (g_mexstatus_boot != ~0UL)
    {
        rt_kprintf("MEXSTATUS: boot 0x%08lx, now 0x%08lx (hw IRQ stack off)\n",
                   (unsigned long)g_mexstatus_boot, mexstatus);
    }
}

void rt_hw_board_init(void)
{
    riscv_isa_ext_set(I);
    riscv_isa_ext_set(M);
    riscv_isa_ext_set(A);
    riscv_isa_ext_set(F);
    riscv_isa_ext_set(C);

    aic_board_clock_init();

    rt_hw_interrupt_init();
    riscv_timer_set_frequency(BSP_CLINT_FREQ);
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
    d12p_show_hardware_info();

#ifdef RT_USING_COMPONENTS_INIT
    rt_components_board_init();
#endif

#ifdef RT_USING_CONSOLE
    rt_kprintf("console: UART0 char device\n");
#endif
}
