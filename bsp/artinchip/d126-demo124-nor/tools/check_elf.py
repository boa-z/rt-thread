#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# SPDX-License-Identifier: Apache-2.0
#
# Sanity checks for the D126 RT-Thread ELF.  These checks catch the most
# common bring-up mistakes before an image is packaged:
#
#   - wrong ELF class / ABI / ISA
#   - entry point not at the PSRAM load address
#   - vector table not 64-byte aligned or not inside a loadable segment
#   - stack/heap overlap
#
# They are BUILD-level checks only; they do not prove that the image boots.

import argparse
import os
import re
import shutil
import subprocess
import sys


BSP_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PSRAM_BASE = 0x40000000
EXPECTED_MACHINE = "RISC-V"
EXPECTED_CLASS = "ELF32"
EXPECTED_FLAGS = ("RVC", "single-float ABI")


def tool(name):
    prefix = os.getenv("RTT_CC_PREFIX", "riscv64-unknown-elf-")
    exec_path = os.getenv("RTT_EXEC_PATH", "")
    candidate = os.path.join(exec_path, prefix + name) if exec_path else prefix + name
    if os.path.isfile(candidate) or shutil.which(candidate):
        return candidate
    return name


def run(cmd):
    return subprocess.check_output(cmd, stderr=subprocess.STDOUT).decode(
        "utf-8", "replace")


def fail(message):
    print("FAIL: " + message)
    return 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", default=os.path.join(BSP_DIR, "rtthread.elf"))
    args = parser.parse_args()

    if not os.path.isfile(args.elf):
        sys.exit("missing ELF: " + args.elf)

    errors = 0
    readelf = tool("readelf")
    nm = tool("nm")

    header = run([readelf, "-h", args.elf])
    if EXPECTED_CLASS not in header:
        errors += fail("expected ELF32")
    if EXPECTED_MACHINE not in header:
        errors += fail("expected RISC-V machine")
    for flag in EXPECTED_FLAGS:
        if flag not in header:
            errors += fail("missing ELF flag: " + flag)

    match = re.search(r"Entry point address:\s+0x([0-9a-fA-F]+)", header)
    if not match:
        errors += fail("cannot read entry point")
        entry = 0
    else:
        entry = int(match.group(1), 16)

    segments = run([readelf, "-l", args.elf])
    loads = re.findall(
        r"LOAD\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)"
        r"\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)",
        segments)
    if not loads:
        errors += fail("no PT_LOAD segment found")
        loads = []

    if entry != PSRAM_BASE:
        errors += fail("entry point 0x%x != 0x%x" % (entry, PSRAM_BASE))

    first_vaddr = int(loads[0][1], 16) if loads else 0
    if first_vaddr != PSRAM_BASE:
        errors += fail("first LOAD vaddr 0x%x != 0x%x" %
                       (first_vaddr, PSRAM_BASE))

    symbols = {}
    for line in run([nm, "-n", args.elf]).splitlines():
        parts = line.split()
        if len(parts) >= 3:
            symbols[parts[2]] = int(parts[0], 16)

    required = [
        "_start", "Reset_Handler", "__Vectors", "Default_IRQHandler",
        "PendSV_Handler", "Default_Handler", "__irq_stack_bottom",
        "__rt_rvstack", "__heap_start", "__heap_end", "__bss_end__",
    ]
    for name in required:
        if name not in symbols:
            errors += fail("missing symbol: " + name)

    if errors == 0:
        if symbols["_start"] != entry:
            errors += fail("_start is not the ELF entry point")
        if symbols["__Vectors"] % 64 != 0:
            errors += fail("__Vectors is not 64-byte aligned")
        if symbols["Default_Handler"] % 64 != 0:
            errors += fail("Default_Handler (mtvec base) is not 64-byte aligned")
        if symbols["PendSV_Handler"] % 4 != 0:
            errors += fail("PendSV_Handler is not 4-byte aligned")
        if symbols["__irq_stack_bottom"] >= symbols["__rt_rvstack"]:
            errors += fail("boot stack is empty")
        if symbols["__rt_rvstack"] > symbols["__heap_start"]:
            errors += fail("boot stack overlaps heap")
        if symbols["__heap_start"] >= symbols["__heap_end"]:
            errors += fail("heap is empty")
        if symbols["__bss_end__"] > symbols["__heap_start"]:
            errors += fail("BSS overlaps heap")

        vectors = symbols["__Vectors"]
        loaded = False
        for _off, vaddr, _paddr, filesz, _memsz in loads:
            start = int(vaddr, 16)
            end = start + int(filesz, 16)
            if start <= vectors < end:
                loaded = True
        if not loaded:
            errors += fail("__Vectors is not inside a file-backed LOAD segment")

    if errors:
        print("D126 ELF check: FAIL (%d error(s))" % errors)
        return 1

    print("D126 ELF check: PASS")
    print("  entry        : 0x%08x" % entry)
    print("  start        : 0x%08x" % symbols["_start"])
    print("  vectors      : 0x%08x (aligned)" % symbols["__Vectors"])
    print("  boot stack   : 0x%08x..0x%08x" % (
        symbols["__irq_stack_bottom"], symbols["__rt_rvstack"]))
    print("  heap         : 0x%08x..0x%08x (%u KiB)" % (
        symbols["__heap_start"], symbols["__heap_end"],
        (symbols["__heap_end"] - symbols["__heap_start"]) // 1024))
    return 0


if __name__ == "__main__":
    sys.exit(main())
