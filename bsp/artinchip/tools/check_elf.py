#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# SPDX-License-Identifier: Apache-2.0
#
# Static checks for ArtInChip RT-Thread RISC-V BSPs using the upstream
# Next RISC-V common port.
#
# These checks catch the migration errors that a successful link does not:
#   - wrong ELF class / ABI / entry
#   - legacy BSP startup or trap objects still linked
#   - the old E907 cpu/ port still linked beside libcpu
#   - BSS/percpu/heap overlap
#
# BUILD/STATIC PASS is not hardware validation.

import argparse
import os
import re
import shutil
import subprocess
import sys


def find_tool(name):
    prefix = os.getenv("RTT_CC_PREFIX", "riscv64-unknown-elf-")
    exec_path = os.getenv("RTT_EXEC_PATH", "")
    candidate = os.path.join(exec_path, prefix + name) if exec_path else prefix + name
    if os.path.isfile(candidate) or shutil.which(candidate):
        return candidate
    return name


def run(cmd):
    return subprocess.check_output(cmd, stderr=subprocess.STDOUT).decode(
        "utf-8", "replace")


def fail(errors, message):
    print("FAIL: " + message)
    errors.append(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True)
    parser.add_argument("--map", default=None)
    parser.add_argument("--xlen", type=int, choices=(32, 64), required=True)
    parser.add_argument("--entry", default="0x40000000")
    parser.add_argument("--intc", choices=("plic", "clic"), required=True)
    args = parser.parse_args()

    if not os.path.isfile(args.elf):
        sys.exit("missing ELF: " + args.elf)

    errors = []
    readelf = find_tool("readelf")
    nm = find_tool("nm")
    entry = int(args.entry, 16)

    header = run([readelf, "-h", args.elf])
    expected_class = "ELF32" if args.xlen == 32 else "ELF64"
    expected_abi = "single-float ABI" if args.xlen == 32 else "double-float ABI"
    if expected_class not in header:
        fail(errors, "expected " + expected_class)
    if "RISC-V" not in header:
        fail(errors, "expected RISC-V machine")
    if "RVC" not in header:
        fail(errors, "expected RVC")
    if expected_abi not in header:
        fail(errors, "expected " + expected_abi)

    m = re.search(r"Entry point address:\s+0x([0-9a-fA-F]+)", header)
    if not m:
        sys.exit("cannot parse ELF entry point")
    if int(m.group(1), 16) != entry:
        fail(errors, "entry point 0x%s != 0x%x" % (m.group(1), entry))

    symbols = {}
    starts = []
    for line in run([nm, args.elf]).splitlines():
        parts = line.split()
        if len(parts) >= 3:
            name = parts[2]
            symbols[name] = int(parts[0], 16)
            if name == "_start":
                starts.append(name)

    if len(starts) != 1:
        fail(errors, "expected exactly one _start, found %d" % len(starts))

    required = [
        "_start", "__text_start", "system_vectors", "rt_hw_trap",
        "rt_hw_interrupt_init", "rt_hw_context_switch",
        "rt_hw_context_switch_interrupt", "riscv_timer_rdtime",
        "riscv_timer_set_compare", "__bss_start", "__bss_end",
        "__percpu_start", "__percpu_end", "__heap_start", "__heap_end",
        "_end",
    ]
    required.append("platform_get_plic_base" if args.intc == "plic"
                    else "platform_get_clic_base")
    for name in required:
        if name not in symbols:
            fail(errors, "missing symbol: " + name)

    forbidden = [
        "__Vectors", "Default_IRQHandler", "PendSV_Handler",
        "Default_Handler", "trap_entry", "SW_handler",
    ]
    for name in forbidden:
        if name in symbols:
            fail(errors, "legacy symbol still linked: " + name)

    if not errors:
        if symbols["_start"] != entry:
            fail(errors, "_start is not the ELF entry point")
        if symbols["__text_start"] != entry:
            fail(errors, "__text_start is not the ELF entry point")
        if symbols["system_vectors"] % 64 != 0:
            fail(errors, "system_vectors is not 64-byte aligned")
        if symbols["__bss_start"] > symbols["__bss_end"]:
            fail(errors, "BSS range is invalid")
        if symbols["__bss_end"] > symbols["__percpu_start"]:
            fail(errors, "BSS overlaps .percpu")
        if symbols["__percpu_start"] > symbols["__percpu_end"]:
            fail(errors, ".percpu range is invalid")
        if symbols["__percpu_end"] > symbols["__heap_start"]:
            fail(errors, ".percpu overlaps heap")
        if symbols["_end"] > symbols["__heap_start"]:
            fail(errors, "image end overlaps heap")
        if symbols["__heap_start"] >= symbols["__heap_end"]:
            fail(errors, "heap is empty")

    if args.map:
        if not os.path.isfile(args.map):
            fail(errors, "missing map: " + args.map)
        else:
            with open(args.map, "r", errors="replace") as f:
                text = f.read()
            required_objs = [
                "libcpu/risc-v/common/startup_gcc.o",
                "libcpu/risc-v/common/context_gcc.o",
                "libcpu/risc-v/common/interrupt.o",
            ]
            for obj in required_objs:
                if obj not in text:
                    fail(errors, "map does not contain " + obj)
            forbidden_objs = [
                "libcpu/risc-v/rv64/",
                "libcpu/risc-v/common64/",
                "libcpu/risc-v/legacy_common/",
                "board/start_gcc.o",
                "cpu/context_gcc.o",
                "board/interrupt_gcc.o",
                "board/startup_gcc.o",
            ]
            for obj in forbidden_objs:
                if obj in text:
                    fail(errors, "legacy object still linked: " + obj)

    if errors:
        print("ArtInChip RISC-V check: FAIL (%d error(s))" % len(errors))
        return 1

    print("ArtInChip RISC-V check: PASS")
    print("  ELF          : %s, %s" % (expected_class, expected_abi))
    print("  entry/_start : 0x%08x" % entry)
    print("  trap vector  : 0x%08x (direct mode)" % symbols["system_vectors"])
    print("  controler    : %s" % args.intc.upper())
    print("  BSS          : 0x%08x..0x%08x" % (
        symbols["__bss_start"], symbols["__bss_end"]))
    print("  percpu       : 0x%08x..0x%08x" % (
        symbols["__percpu_start"], symbols["__percpu_end"]))
    print("  heap         : 0x%08x..0x%08x (%u KiB)" % (
        symbols["__heap_start"], symbols["__heap_end"],
        (symbols["__heap_end"] - symbols["__heap_start"]) // 1024))
    return 0


if __name__ == "__main__":
    sys.exit(main())
