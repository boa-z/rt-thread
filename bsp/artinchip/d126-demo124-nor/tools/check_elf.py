#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""D126 wrapper for the shared ArtInChip RISC-V ELF checker."""

import argparse
import os
import subprocess
import sys

BSP_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CHECKER = os.path.join(BSP_DIR, "..", "tools", "check_elf.py")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", default=os.path.join(BSP_DIR, "rtthread.elf"))
    parser.add_argument("--map", default=os.path.join(BSP_DIR, "rtthread.map"))
    args = parser.parse_args()

    cmd = [sys.executable, os.path.normpath(CHECKER),
           "--elf", args.elf, "--map", args.map,
           "--xlen", "32", "--entry", "0x40000000", "--intc", "clic"]
    return subprocess.call(cmd)


if __name__ == "__main__":
    sys.exit(main())
