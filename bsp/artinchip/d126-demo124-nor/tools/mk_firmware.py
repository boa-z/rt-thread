#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# SPDX-License-Identifier: Apache-2.0
#
# Build an ArtInChip D12p (D126) firmware image from a standard
# RT-Thread ELF.
#
# The vendor bootloader and PBP binary are not part of this BSP.  Pass
# --bootloader with a bootloader.bin built for target/d12p/d126-demo124-nor,
# or let the script look for it in the Luban-Lite build output.  The
# --allow-placeholder-bootloader option is only for CI validation of the
# packaging flow; the resulting image is NOT bootable and must not be
# flashed.
#
# The script never flashes a device.

import argparse
import glob
import os
import shutil
import subprocess
import sys


BSP_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PACK_DIR = os.path.join(BSP_DIR, "pack")

BOOTLOADER_TEXT_SIZE = 0x7FF00
BOOTLOADER_HEAP_SIZE = 0xC0000
PSRAM_BASE = 0x40000000


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


def bootloader_addresses(psram_size):
    text_base = PSRAM_BASE + psram_size - BOOTLOADER_HEAP_SIZE - \
        BOOTLOADER_TEXT_SIZE
    load_base = text_base - 0x100
    return load_base, text_base


def is_placeholder_bootloader(path):
    try:
        with open(path, "rb") as f:
            return b"D126 PLACEHOLDER BOOTLOADER" in f.read(128)
    except OSError:
        return False


def resolve_bootloader(args, sdk, out):
    candidates = []
    if args.bootloader:
        candidates.append(args.bootloader)
    candidates.append(os.path.join(sdk, "target", "d12p",
                                   "d126-demo124-nor", "pack",
                                   "bootloader.bin"))
    candidates.append(os.path.join(sdk, "output", "d12p",
                                   "d126-demo124-nor",
                                   "bootloader", "bootloader.bin"))

    for candidate in candidates:
        if candidate and os.path.isfile(candidate):
            if is_placeholder_bootloader(candidate) and \
                    not args.allow_placeholder_bootloader:
                sys.exit("refusing to package a placeholder bootloader "
                         "without --allow-placeholder-bootloader")
            return os.path.abspath(candidate), False

    if not args.allow_placeholder_bootloader:
        sys.exit(
            "bootloader.bin not found.  Build the D12p bootloader with the "
            "vendor SDK, then pass --bootloader /path/to/bootloader.bin.\n"
            "For packaging-flow validation only, pass "
            "--allow-placeholder-bootloader (the image will not boot).")

    placeholder = os.path.join(out, "bootloader.placeholder.bin")
    marker = b"D126 PLACEHOLDER BOOTLOADER - DO NOT FLASH\n"
    with open(placeholder, "wb") as f:
        f.write(marker)
        f.write(b"\xff" * (0x1000 - len(marker)))
    print("WARNING: using a placeholder bootloader; the image is NOT bootable")
    return placeholder, True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", default=find_default_sdk(),
                        help="Luban-Lite SDK root (or set AIC_SDK_DIR)")
    parser.add_argument("--elf", default=os.path.join(BSP_DIR, "rtthread.elf"),
                        help="input RT-Thread ELF")
    parser.add_argument("--out", default=os.path.join(BSP_DIR, "images"),
                        help="output directory")
    parser.add_argument("--bootloader", default=None,
                        help="vendor bootloader.bin for d126-demo124-nor")
    parser.add_argument("--psram-size", default="8m",
                        help="PSRAM size (8m or 16m), default 8m")
    parser.add_argument("--allow-placeholder-bootloader", action="store_true",
                        help="CI only: package with a non-bootable placeholder")
    parser.add_argument("--python", default=None,
                        help="Python interpreter used for vendor scripts")
    args = parser.parse_args()

    if not args.sdk:
        sys.exit("please pass --sdk or set AIC_SDK_DIR")
    sdk = os.path.abspath(args.sdk)
    elf = os.path.abspath(args.elf)
    out = os.path.abspath(args.out)

    psram_size = args.psram_size.lower().replace("_", "").replace(" ", "")
    if psram_size in ("8m", "8mb", "0x800000"):
        psram_size = 0x800000
    elif psram_size in ("16m", "16mb", "0x1000000"):
        psram_size = 0x1000000
    else:
        sys.exit("unsupported --psram-size (use 8m or 16m)")

    scripts = os.path.join(sdk, "tools", "scripts")
    mk_image = os.path.join(scripts, "mk_image.py")
    vendor_pbp = os.path.join(sdk, "bsp", "artinchip", "sys", "d12p",
                              "d12p.pbp")

    require_file(elf, "RT-Thread ELF")
    require_file(mk_image, "ArtInChip mk_image.py")
    require_file(vendor_pbp, "D12p d12p.pbp")

    python = args.python or sys.executable
    if not os.path.isfile(python) and shutil.which(python) is None:
        sys.exit("Python interpreter not found: {}".format(python))

    os.makedirs(out, exist_ok=True)

    shutil.copy2(elf, os.path.join(out, "rtthread.elf"))
    shutil.copy2(os.path.join(PACK_DIR, "image_cfg.json"),
                 os.path.join(out, "image_cfg.json"))
    shutil.copy2(os.path.join(PACK_DIR, "env.txt"),
                 os.path.join(out, "env.txt"))
    shutil.copy2(os.path.join(PACK_DIR, "pbp_cfg.json"),
                 os.path.join(out, "pbp_cfg.json"))
    shutil.copy2(vendor_pbp, os.path.join(out, "d12p.pbp"))

    bootloader, placeholder = resolve_bootloader(args, sdk, out)
    shutil.copy2(bootloader, os.path.join(out, "bootloader.bin"))

    load_base, text_base = bootloader_addresses(psram_size)
    cfg_path = os.path.join(out, "image_cfg.json")
    with open(cfg_path, "r", encoding="utf-8") as f:
        cfg = f.read()
    cfg = cfg.replace("__AIC_BOOTLOADER_LOAD__", hex(load_base))
    cfg = cfg.replace("__AIC_BOOTLOADER_ENTRY__", hex(text_base))
    with open(cfg_path, "w", encoding="utf-8") as f:
        f.write(cfg)

    env = os.environ.copy()
    env["PATH"] = scripts + os.pathsep + env.get("PATH", "")
    env["PYTHONPATH"] = scripts + os.pathsep + env.get("PYTHONPATH", "")

    if placeholder:
        print("bootloader: placeholder (load {}, entry {})".format(
            hex(load_base), hex(text_base)))
    else:
        print("bootloader: {} (load {}, entry {})".format(
            bootloader, hex(load_base), hex(text_base)))

    run([python, mk_image, "-v", "-c", cfg_path, "-d", out + os.sep],
        env=env)

    images = sorted(glob.glob(os.path.join(out, "*.img")))
    if not images:
        sys.exit("expected firmware image was not generated")

    if placeholder:
        renamed = []
        for image in images:
            target = image[:-4] + "_PLACEHOLDER_DO_NOT_FLASH.img"
            os.replace(image, target)
            renamed.append(target)
        images = renamed
        with open(os.path.join(out, "DO_NOT_FLASH.txt"), "w",
                  encoding="utf-8") as f:
            f.write("This image was packaged with a placeholder bootloader.\n")
            f.write("It is NOT bootable and must NOT be flashed.\n")
            f.write("Build a real d126-demo124-nor bootloader and rerun\n")
            f.write("mk_firmware.py without --allow-placeholder-bootloader.\n")

    print("")
    print("Firmware output: {}".format(out))
    for image in images:
        print("  {} ({} bytes)".format(
            os.path.basename(image), os.path.getsize(image)))
    if placeholder:
        print("")
        print("WARNING: this image contains a placeholder bootloader and")
        print("is NOT bootable.  Do not flash it.")


if __name__ == "__main__":
    main()
