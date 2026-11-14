/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D126 (D12p) memory layout for the RT-Thread RISC-V common
 * port.
 *
 * The vendor bootloader loads the FIT image into PSRAM at 0x40000000.
 * The common startup creates its early per-CPU stack in .percpu and the
 * common IRQ stack lives in .bss.  The remaining PSRAM is the heap.
 */

#include <rtconfig.h>

#ifndef AIC_PSRAM_SIZE
#define AIC_PSRAM_SIZE 0x00800000
#endif

#ifndef RT_CPUS_NR
#define RT_CPUS_NR 1
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
    } > PSRAM

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
    } > PSRAM

    .percpu (NOLOAD) : {
        __percpu_start = .;
        *(.percpu)
        __percpu_end = .;
        . = __percpu_end + (__percpu_end - __percpu_start) * (RT_CPUS_NR - 1);
        __percpu_real_end = .;
    } > PSRAM

    _end = .;
    __bss_start__ = __bss_start;
    __bss_end__ = __bss_end;

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
ASSERT(__bss_end <= __heap_start, "bss overlaps heap")
ASSERT(__percpu_start <= __percpu_end, "percpu section is invalid")
ASSERT(_end <= __heap_start, "image end overlaps heap")
ASSERT(__heap_start < __heap_end, "heap is empty")
