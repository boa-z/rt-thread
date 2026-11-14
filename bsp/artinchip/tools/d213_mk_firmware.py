#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# SPDX-License-Identifier: Apache-2.0
#
# Build an ArtInChip-compatible D213 firmware image from a standard
# RT-Thread ELF.  This script is intentionally separate from SCons: the
# BSP build produces rtthread.elf, and this tool performs the vendor
# ELF -> ITS -> ITB -> IMG packaging step.
#
# It uses the ArtInChip Luban-Lite image tools and the original D213
# bootloader/DDR/PBP assets.  Those files are not part of this BSP and
# must be supplied by the user:
#
#   AIC_SDK_DIR=/path/to/luban-lite-jc \
#       python bsp/artinchip/tools/d213_mk_firmware.py \
#           --elf rtthread.elf
#
# The script never flashes a device.

import argparse
import os
import shutil
import subprocess
import sys


BSP_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                       "d213ecv-demo-v4-0")
PACK_DIR = os.path.join(BSP_DIR, "pack")


def find_default_sdk():
    for name in ("AIC_SDK_DIR", "LUBAN_LITE_DIR"):
        value = os.environ.get(name)
        if value:
            return os.path.abspath(value)
    return None


def require_file(path, label):
    if not os.path.isfile(path):
        sys.exit("missing {}: {}".format(label, path))


def run(cmd, cwd=None, env=None):
    print("+ " + " ".join(cmd))
    ret = subprocess.call(cmd, cwd=cwd, env=env)
    if ret != 0:
        sys.exit("command failed with exit code {}".format(ret))


def validate_spl_size(path):
    # nand_fwc_spl.c: ceil(SPL bytes / NAND page size) + one table page.
    # This D213 package targets 2 KiB NAND pages and a 202-entry table.
    size = os.path.getsize(path)
    entries = (size + 2047) // 2048 + 1
    if size == 0 or entries > 202:
        raise ValueError("SPL {} bytes requires {} page-table entries; "
                         "maximum is 202 (411648 payload bytes)".format(
                             size, entries))
    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", default=find_default_sdk(),
                        help="Luban-Lite SDK root (or set AIC_SDK_DIR)")
    parser.add_argument("--elf", default=os.path.join(BSP_DIR, "rtthread.elf"),
                        help="input RT-Thread ELF")
    parser.add_argument("--out", default=os.path.join(BSP_DIR, "images"),
                        help="output directory")
    parser.add_argument("--python", default=None,
                        help="Python interpreter used for vendor scripts "
                             "(default: the interpreter running this script, "
                             "which should be the uv virtual environment)")
    args = parser.parse_args()

    if not args.sdk:
        sys.exit("please pass --sdk or set AIC_SDK_DIR")
    sdk = os.path.abspath(args.sdk)
    elf = os.path.abspath(args.elf)
    out = os.path.abspath(args.out)

    scripts = os.path.join(sdk, "tools", "scripts")
    vendor_bootloader = os.path.join(sdk, "target", "d21x", "demo128-nand",
                                     "pack", "bootloader.bin")
    vendor_pbp = os.path.join(sdk, "bsp", "artinchip", "sys", "d21x",
                              "d21x.pbp")
    mk_image = os.path.join(scripts, "mk_image.py")
    gen_part = os.path.join(scripts, "gen_partition_table.py")
    mk_priv = os.path.join(scripts, "mk_private_resource.py")

    require_file(elf, "RT-Thread ELF")
    require_file(mk_image, "ArtInChip mk_image.py")
    require_file(gen_part, "ArtInChip gen_partition_table.py")
    require_file(mk_priv, "ArtInChip mk_private_resource.py")
    require_file(vendor_bootloader, "D213 bootloader.bin")
    require_file(vendor_pbp, "D213 d21x.pbp")

    # Always use the interpreter that runs this script by default.  This is
    # the uv virtual environment and is valid on Windows, Linux and macOS;
    # the SDK's bundled Python is a Windows executable and must not be
    # selected on other hosts.
    python = args.python or sys.executable
    if not os.path.isfile(python) and shutil.which(python) is None:
        sys.exit("Python interpreter not found: {}".format(python))

    os.makedirs(out, exist_ok=True)

    shutil.copy2(elf, os.path.join(out, "rtthread.elf"))
    shutil.copy2(os.path.join(PACK_DIR, "image_cfg.json"), out)
    shutil.copy2(os.path.join(PACK_DIR, "env.txt"), out)
    shutil.copy2(os.path.join(PACK_DIR, "ddr_init.json"), out)
    shutil.copy2(vendor_bootloader, out)
    shutil.copy2(vendor_pbp, out)

    env = os.environ.copy()
    env["PATH"] = scripts + os.pathsep + env.get("PATH", "")

    # 1. Partition table and private DDR resource.
    run([python, gen_part,
         "-c", os.path.join(out, "image_cfg.json"),
         "-o", os.path.join(out, "partition_table.h"),
         "-j", os.path.join(out, "partition.json")], env=env)
    run([python, mk_priv, "-v",
         "-l", "{},{}".format(os.path.join(out, "ddr_init.json"),
                              os.path.join(out, "partition.json")),
         "-o", os.path.join(out, "ddr_init.bin")], env=env)

    # 2. Pre-process bootloader/ITB/env and assemble the final image.
    run([python, mk_image, "-v",
         "-c", os.path.join(out, "image_cfg.json"),
         "-d", out + os.sep], env=env)

    image = os.path.join(
        out, "d21x_d213_demo128_page_2k_block_128k_v1.0.0.img")
    if not os.path.isfile(image):
        sys.exit("expected firmware image was not generated: " + image)

    # Older SDK packers may ignore concatenate.max_size. Do not deliver an
    # image whose SPL the NAND upgrade path cannot represent.
    try:
        entries = validate_spl_size(os.path.join(out, "bootloader.aic"))
    except (OSError, ValueError) as exc:
        os.remove(image)
        sys.exit("invalid D213 NAND SPL: " + str(exc))
    print("SPL page-table entries: {}/202".format(entries))

    print("")
    print("Firmware output: {}".format(out))
    print("  bootloader.aic")
    print("  d21x_os.itb")
    print("  d21x_d213_demo128_page_2k_block_128k_v1.0.0.img")


if __name__ == "__main__":
    main()
