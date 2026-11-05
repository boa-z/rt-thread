/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D126 (D12p) memory layout.
 *
 * The vendor bootloader loads the FIT image into PSRAM at 0x40000000.
 * The whole RT-Thread image, its boot stack and the system heap live in
 * PSRAM.  No DSP/CMA carve-out is made yet; display support will add a
 * reserved region when the panel is confirmed.
 */

#include <rtconfig.h>

#ifndef AIC_PSRAM_SIZE
#define AIC_PSRAM_SIZE 0x00800000
#endif

#ifndef AIC_BOOT_STACK_SIZE
#define AIC_BOOT_STACK_SIZE 0x2000
#endif

OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
    PSRAM (rwx) : ORIGIN = 0x40000000, LENGTH = AIC_PSRAM_SIZE
}

SECTIONS
{
    . = ORIGIN(PSRAM);

    .text : {
        KEEP(*(.text.start))
        KEEP(*(.text.entry))
        KEEP(*(.text.trap))
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
    } > PSRAM

    .rodata : {
        . = ALIGN(8);
        __srodata = .;
        KEEP(*(.vectors))
        *(.rodata)
        *(.rodata.*)
        *(.srodata)
        *(.srodata.*)
        *(.rdata)
        *(.rdata.*)
        . = ALIGN(8);
        __rodata_end__ = .;
    } > PSRAM

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
    } > PSRAM

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
    } > PSRAM

    . = ALIGN(16);
    __irq_stack_bottom = .;
    . += AIC_BOOT_STACK_SIZE;
    __rt_rvstack = .;

    . = ALIGN(16);
    __heap_start = .;
    __heap_end = ORIGIN(PSRAM) + LENGTH(PSRAM);

    /DISCARD/ : {
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.comment)
    }
}

ASSERT(__data_end__ <= __bss_start__, "data overlaps bss")
ASSERT(__bss_end__ <= __heap_start, "bss overlaps heap")
ASSERT(__irq_stack_bottom < __rt_rvstack, "boot stack is empty")
ASSERT(__heap_start < __heap_end, "heap is empty")
