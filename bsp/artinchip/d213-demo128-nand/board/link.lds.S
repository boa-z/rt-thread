/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D213 memory layout.
 *
 * The vendor bootloader loads segment 0 of the FIT image at 0x40000000
 * and enters at the same address.  The image is linked as one contiguous
 * DRAM image; BSS is cleared by start_gcc.S.
 */

#include <rtconfig.h>

#ifndef AIC_DRAM_TOTAL_SIZE
#define AIC_DRAM_TOTAL_SIZE 0x8000000
#endif

#ifndef AIC_INTERRUPT_STACK_SIZE
#define AIC_INTERRUPT_STACK_SIZE 0x2000
#endif

#ifndef AIC_BOOTLOADER_RESERVED_SIZE
#define AIC_BOOTLOADER_RESERVED_SIZE 0x800000
#endif

#ifndef AIC_FB_RESERVED_SIZE
#define AIC_FB_RESERVED_SIZE 0
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
        /* The BSP entry must be the first word of the image.  The upstream
         * CPU port also places SW_handler in .text.entry, so keep a
         * dedicated section for _start ahead of it. */
        KEEP(*(.text.start))
        KEEP(*(.text.entry))
        *(.text)
        *(.text.*)
        *(.gnu.linkonce.t*)
        . = ALIGN(8);
        __fsymtab_start = .;
        KEEP(*(FSymTab))
        __fsymtab_end = .;
        . = ALIGN(8);
        __vsymtab_start = .;
        KEEP(*(VSymTab))
        __vsymtab_end = .;
        . = ALIGN(8);
        __rt_init_start = .;
        KEEP(*(SORT(.rti_fn*)))
        __rt_init_end = .;
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
        __bss_start__ = .;
        *(.bss)
        *(.bss.*)
        *(.sbss)
        *(.sbss.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end__ = .;
        end = .;
    } > DRAM

    . = ALIGN(16);
    __irq_stack_bottom = .;
    . += AIC_INTERRUPT_STACK_SIZE;
    __rt_rvstack = .;

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
ASSERT(__irq_stack_bottom < __rt_rvstack, "interrupt stack is empty")
ASSERT(__heap_start < __heap_end, "heap is empty")
ASSERT(__heap_end <= __fb_start, "heap overlaps the framebuffer reserve")
ASSERT(__fb_start <= __fb_end, "framebuffer reserve is invalid")
ASSERT(__fb_end <= ORIGIN(DRAM) + LENGTH(DRAM) - AIC_BOOTLOADER_RESERVED_SIZE,
       "framebuffer exceeds the non-reserved DDR area")
