#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# SPDX-License-Identifier: Apache-2.0
#
# One-command build/static-check/package matrix for the ArtInChip BSPs.
#
# The script backs up each BSP .config/rtconfig.h, applies a saved profile,
# regenerates rtconfig.h, builds, runs the shared ELF/map checker, optionally
# packages firmware, and restores the original files.
#
# It never flashes hardware.

import argparse
import datetime
import hashlib
import os
import shutil
import subprocess
import sys


ARTICHIP_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RTT_ROOT = os.path.normpath(os.path.join(ARTICHIP_DIR, "..", ".."))
ARTIFACTS = os.path.join(RTT_ROOT, "artifacts")
CHECKER = os.path.join(ARTICHIP_DIR, "tools", "check_elf.py")

TARGETS = {
    "d213-minimal": {
        "bsp": os.path.join(ARTICHIP_DIR, "d213-demo128-nand"),
        "config": "configs/d213-minimal.config",
        "xlen": 64,
        "intc": "plic",
        "package": True,
    },
    "d213-default": {
        "bsp": os.path.join(ARTICHIP_DIR, "d213-demo128-nand"),
        "config": "configs/d213-default.config",
        "xlen": 64,
        "intc": "plic",
        "package": True,
    },
    "d126-minimal": {
        "bsp": os.path.join(ARTICHIP_DIR, "d126-demo124-nor"),
        "config": "configs/d126-minimal.config",
        "xlen": 32,
        "intc": "clic",
        "package": True,
    },
}


def run(cmd, cwd=None, log=None, check=True):
    print("+ " + " ".join(cmd))
    with open(log, "a", encoding="utf-8") as f:
        f.write("+ " + " ".join(cmd) + "\n")
        f.flush()
        ret = subprocess.call(cmd, cwd=cwd, stdout=f, stderr=subprocess.STDOUT)
    if check and ret != 0:
        raise RuntimeError("command failed (%d): %s" % (ret, " ".join(cmd)))
    return ret


def scons_cmd():
    candidate = shutil.which("scons")
    if candidate:
        return candidate
    venv_scons = os.path.join(RTT_ROOT, ".venv", "bin", "scons")
    if os.path.isfile(venv_scons):
        return venv_scons
    return "scons"


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def tool_version():
    prefix = os.getenv("RTT_CC_PREFIX", "riscv64-unknown-elf-")
    gcc = shutil.which(prefix + "gcc")
    if not gcc:
        return "unknown"
    try:
        out = subprocess.check_output([gcc, "--version"], text=True)
        return out.splitlines()[0]
    except Exception:
        return "unknown"


def defconfig(bsp):
    snippet = (
        "import sys; sys.path.insert(0, %r); "
        "import env_utility; env_utility.defconfig(%r)"
        % (os.path.join(RTT_ROOT, "tools"), RTT_ROOT)
    )
    subprocess.check_call([sys.executable, "-c", snippet], cwd=bsp)


def backup(path):
    if os.path.exists(path):
        shutil.copy2(path, path + ".build-check.bak")


def restore(path):
    bak = path + ".build-check.bak"
    if os.path.exists(bak):
        shutil.move(bak, path)


def package(bsp, target, outdir, sdk, log):
    script = os.path.join(bsp, "tools", "mk_firmware.py")
    if not os.path.isfile(script):
        return "SKIP: no mk_firmware.py"
    if not sdk:
        return "SKIP: no --sdk"
    if not os.path.isfile(os.path.join(bsp, "rtthread.elf")):
        return "FAIL: no ELF"

    out = os.path.join(outdir, "images")
    os.makedirs(out, exist_ok=True)
    cmd = [sys.executable, script, "--sdk", sdk,
           "--elf", os.path.join(bsp, "rtthread.elf"), "--out", out]
    if target.startswith("d126"):
        # Never present a placeholder image as flashable.
        cmd.append("--allow-placeholder-bootloader")
    ret = run(cmd, cwd=bsp, log=log, check=False)
    return "PASS" if ret == 0 else "FAIL"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--targets", nargs="*", default=list(TARGETS),
                        choices=list(TARGETS))
    parser.add_argument("--sdk", default=os.getenv("AIC_SDK_DIR"))
    parser.add_argument("--jobs", default="8")
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()

    if not os.path.isfile(CHECKER):
        sys.exit("missing shared checker: " + CHECKER)

    os.makedirs(ARTIFACTS, exist_ok=True)
    results = []
    stamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    for name in args.targets:
        info = TARGETS[name]
        bsp = info["bsp"]
        outdir = os.path.join(ARTIFACTS, name)
        os.makedirs(outdir, exist_ok=True)
        log = os.path.join(outdir, "build.log")
        with open(log, "w", encoding="utf-8") as f:
            f.write("ArtInChip build-check: %s\n" % name)
            f.write("date: %s\n" % stamp)
            f.write("bsp: %s\n" % bsp)

        cfg = os.path.join(bsp, info["config"])
        if not os.path.isfile(cfg):
            results.append((name, "FAIL", "missing config " + cfg))
            continue

        config_path = os.path.join(bsp, ".config")
        rtconfig_path = os.path.join(bsp, "rtconfig.h")
        link_path = os.path.join(bsp, "link.lds")
        backup(config_path)
        backup(rtconfig_path)

        try:
            shutil.copy2(cfg, config_path)
            defconfig(bsp)
            if not args.skip_build:
                run([scons_cmd(), "-c"], cwd=bsp, log=log, check=False)
                run([scons_cmd(), "-j", args.jobs], cwd=bsp, log=log)

            check = run([sys.executable, CHECKER,
                         "--elf", os.path.join(bsp, "rtthread.elf"),
                         "--map", os.path.join(bsp, "rtthread.map"),
                         "--xlen", str(info["xlen"]),
                         "--entry", "0x40000000",
                         "--intc", info["intc"]],
                        cwd=bsp, log=log, check=False)
            if check != 0:
                results.append((name, "FAIL", "ELF/map check"))
                continue

            shutil.copy2(os.path.join(bsp, "rtthread.elf"),
                         os.path.join(outdir, "rtthread.elf"))
            shutil.copy2(os.path.join(bsp, "rtthread.bin"),
                         os.path.join(outdir, "rtthread.bin"))

            pkg = "SKIP"
            if info["package"]:
                pkg = package(bsp, name, outdir, args.sdk, log)
            status = "PASS" if pkg != "FAIL" else "FAIL"
            results.append((name, status, "build=PASS check=PASS package=" + pkg))
        except Exception as exc:
            results.append((name, "FAIL", str(exc)))
        finally:
            restore(config_path)
            restore(rtconfig_path)

    print("")
    print("ArtInChip build-check summary")
    print("-----------------------------")
    for name, status, detail in results:
        print("%-14s %-4s %s" % (name, status, detail))

    write_manifest(results, stamp, args.sdk)
    return 0 if all(r[1] == "PASS" for r in results) else 1


def write_manifest(results, stamp, sdk):
    path = os.path.join(ARTIFACTS, "manifest.md")
    try:
        head = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=RTT_ROOT, text=True).strip()
    except Exception:
        head = "unknown"
    try:
        upstream = subprocess.check_output(
            ["git", "rev-parse", "upstream/master"], cwd=RTT_ROOT,
            text=True).strip()
    except Exception:
        upstream = "unknown"

    with open(path, "w", encoding="utf-8") as f:
        f.write("# ArtInChip build manifest\n\n")
        f.write("- date: %s\n" % stamp)
        f.write("- BSP commit: `%s`\n" % head)
        f.write("- upstream/master: `%s`\n" % upstream)
        f.write("- toolchain: %s\n" % tool_version())
        f.write("- SDK: `%s`\n\n" % (sdk or "not supplied"))
        f.write("| target | status | detail |\n|---|---|---|\n")
        for name, status, detail in results:
            f.write("| %s | %s | %s |\n" % (name, status, detail))
        f.write("\n## Files\n\n")
        for name, status, detail in results:
            outdir = os.path.join(ARTIFACTS, name)
            if not os.path.isdir(outdir):
                continue
            f.write("### %s\n\n" % name)
            for root, dirs, files in os.walk(outdir):
                dirs.sort()
                for fn in sorted(files):
                    if fn.endswith(".log"):
                        continue
                    full = os.path.join(root, fn)
                    rel = os.path.relpath(full, ARTIFACTS)
                    f.write("- `%s` (%d bytes, `%s`)\n" % (
                        rel, os.path.getsize(full), sha256(full)))
            f.write("\n")
        f.write("## Flashability\n\n")
        f.write("- d213-minimal/default: real packaging requires the "
                "Luban-Lite D21x bootloader assets.\n")
        f.write("- d126-minimal: real packaging requires a D126 "
                "d126-demo124-nor bootloader.  Placeholder images are "
                "renamed *_PLACEHOLDER_DO_NOT_FLASH.img and are not "
                "flashable.\n")
        f.write("- HW verification: PENDING.  BUILD/STATIC/PACKAGE PASS "
                "is not hardware validation.\n")


if __name__ == "__main__":
    sys.exit(main())
