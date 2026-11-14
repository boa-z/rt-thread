/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D213 memory layout for the RT-Thread RISC-V common port.
 *
 * The vendor bootloader loads the FIT image at 0x40000000 and jumps to
 * its entry point.  The common startup clears BSS, creates its own
 * early per-CPU stack in .percpu, and the common IRQ stack lives in
 * .bss.
 */

#include <rtconfig.h>

#ifndef AIC_DRAM_TOTAL_SIZE
#define AIC_DRAM_TOTAL_SIZE 0x8000000
#endif

#ifndef AIC_BOOTLOADER_RESERVED_SIZE
#define AIC_BOOTLOADER_RESERVED_SIZE 0x800000
#endif

#ifndef AIC_FB_RESERVED_SIZE
#define AIC_FB_RESERVED_SIZE 0
#endif

#ifndef RT_CPUS_NR
#define RT_CPUS_NR 1
#endif

OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
    DRAM (rwx) : ORIGIN = 0x40000000, LENGTH = AIC_DRAM_TOTAL_SIZE
}

SECTIONS
{
    . = ORIGIN(DRAM);

    .text : {
        PROVIDE(__text_start = .);
        KEEP(*(.start))
        KEEP(*(.text.start))
        KEEP(*(.text.entry))
        *(.text)
        *(.text.*)
        *(.gnu.linkonce.t*)
        . = ALIGN(8);
        __rt_utest_tc_tab_start = .;
        KEEP(*(UtestTcTab))
        __rt_utest_tc_tab_end = .;
        . = ALIGN(8);
        __fsymtab_start = .;
        KEEP(*(FSymTab))
        __fsymtab_end = .;
        . = ALIGN(8);
        __vsymtab_start = .;
        KEEP(*(VSymTab))
        __vsymtab_end = .;
        . = ALIGN(8);
        __rtmsymtab_start = .;
        KEEP(*(RTMSymTab))
        __rtmsymtab_end = .;
        . = ALIGN(8);
        __rt_init_start = .;
        KEEP(*(SORT(.rti_fn*)))
        __rt_init_end = .;
        . = ALIGN(8);
        __rt_ofw_data_start = .;
        KEEP(*(SORT(.rt_ofw_data.*)))
        __rt_ofw_data_end = .;
        . = ALIGN(8);
        __usbh_class_info_start__ = .;
        KEEP(*(.usbh_class_info))
        __usbh_class_info_end__ = .;
        . = ALIGN(8);
        __etext = .;
    } > DRAM

    .rodata : {
        . = ALIGN(8);
        __srodata = .;
        *(.rodata)
        *(.rodata.*)
        *(.srodata)
        *(.srodata.*)
        *(.rdata)
        *(.rdata.*)
        . = ALIGN(8);
        __ctors_start__ = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        __ctors_end__ = .;
        __dtors_start__ = .;
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        __dtors_end__ = .;
        . = ALIGN(8);
        __rodata_end__ = .;
    } > DRAM

    .data : {
        . = ALIGN(8);
        __data_start__ = .;
        __global_pointer$ = . + 0x800;
        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
        *(.gnu.linkonce.d*)
        . = ALIGN(8);
        __data_end__ = .;
    } > DRAM

    .bss (NOLOAD) : {
        . = ALIGN(8);
        __bss_noclean_start = .;
        *(.bss.noclean.*)
        __bss_noclean_end = .;
        . = ALIGN(8);
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(.sbss)
        *(.sbss.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > DRAM

    .percpu (NOLOAD) : {
        __percpu_start = .;
        *(.percpu)
        __percpu_end = .;
        . = __percpu_end + (__percpu_end - __percpu_start) * (RT_CPUS_NR - 1);
        __percpu_real_end = .;
    } > DRAM

    _end = .;
    __bss_start__ = __bss_start;
    __bss_end__ = __bss_end;

    . = ALIGN(16);
    __heap_start = .;

    /*
     * Framebuffers live at the top of the non-bootloader DDR area.  The
     * heap ends below them, so neither the allocator nor the display DMA
     * can overwrite the other.  AIC_FB_RESERVED_SIZE is zero when the
     * display is disabled, leaving the original heap layout unchanged.
     */
    __fb_end = ORIGIN(DRAM) + LENGTH(DRAM) - AIC_BOOTLOADER_RESERVED_SIZE;
    /* Keep the framebuffer base on a C906 cache line (64 bytes). */
    __fb_start = (__fb_end - AIC_FB_RESERVED_SIZE) & ~0x3F;
    __heap_end = __fb_start;

    /DISCARD/ : {
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.comment)
    }
}

ASSERT(__data_end__ <= __bss_start__, "data overlaps bss")
ASSERT(__bss_end__ <= __heap_start, "bss overlaps heap")
ASSERT(__percpu_start <= __percpu_end, "percpu section is invalid")
ASSERT(__heap_start < __heap_end, "heap is empty")
ASSERT(__heap_end <= __fb_start, "heap overlaps the framebuffer reserve")
ASSERT(__fb_start <= __fb_end, "framebuffer reserve is invalid")
ASSERT(__fb_end <= ORIGIN(DRAM) + LENGTH(DRAM) - AIC_BOOTLOADER_RESERVED_SIZE,
       "framebuffer exceeds the non-reserved DDR area")
