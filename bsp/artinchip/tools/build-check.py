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
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys


ARTICHIP_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RTT_ROOT = os.path.normpath(os.path.join(ARTICHIP_DIR, "..", ".."))
ARTIFACTS = os.path.join(ARTICHIP_DIR, "artifacts")
CHECKER = os.path.join(ARTICHIP_DIR, "tools", "check_elf.py")
D213_BSP = os.path.join(ARTICHIP_DIR, "d213ecv-demo-v4-0")

TARGETS = {
    "d213-minimal": {
        "bsp": D213_BSP,
        "config": "configs/d213-minimal.config",
        "xlen": 64,
        "intc": "plic",
        "package": True,
    },
    "d213-dma": {
        "bsp": D213_BSP,
        "config": "configs/d213-dma.config",
        "xlen": 64,
        "intc": "plic",
        "package": True,
        "require_symbols": [
            "hal_dma_init",
            "hal_dma_irq",
            "aic_dma_alloc",
        ],
        "require_strings": [
            "dmac: driver loaded",
            "dma_test",
        ],
    },
    "d213-dma-utest": {
        "bsp": D213_BSP,
        "config": "configs/d213-dma-utest.config",
        "xlen": 64,
        "intc": "plic",
        "package": True,
        "require_symbols": [
            "utest_init",
            "hal_dma_init",
            "hal_dma_irq",
            "aic_dma_alloc",
        ],
        "require_symbol_counts": [
            "_utest_testcase=11",
        ],
        "require_strings": [
            "bsp.d213.dmac",
            "dmac: driver loaded",
            "dma_test",
            "aicupg",
        ],
    },
    "d213-uart-dma-utest": {
        "bsp": D213_BSP,
        "config": "configs/d213-uart-dma-utest.config",
        "xlen": 64,
        "intc": "plic",
        "package": True,
        "require_symbols": [
            "utest_init",
            "hal_dma_init",
            "aic_uart1_dma_loopback_test",
        ],
        "require_symbol_counts": [
            "_utest_testcase=11",
        ],
        "require_strings": [
            "bsp.d213.uart1.dma",
            "uart_dma_test",
            "dmac: driver loaded",
            "aicupg",
        ],
    },
    "d213-default": {
        "bsp": D213_BSP,
        "config": "configs/d213-default.config",
        "xlen": 64,
        "intc": "plic",
        "package": True,
    },
    "d213-utest": {
        "bsp": D213_BSP,
        "config": "configs/d213-utest.config",
        "xlen": 64,
        "intc": "plic",
        "package": True,
        "require_symbols": [
            "utest_init",
            "utest_testcase_run",
            "__rt_utest_tc_tab_start",
            "__rt_utest_tc_tab_end",
            "rt_mtd_nand_move_page",
        ],
        "require_symbol_counts": [
            "_utest_testcase=23",
        ],
        "require_strings": [
            "components.utilities.utest.uassert",
            "bsp.d213.smoke",
            "bsp.d213.keyadc",
            "bsp.d213.fpu.context",
            "bsp.d213.can0.config",
            "bsp.d213.spinand.ro",
            "bsp.d213.spinand.rw",
            "bsp.d213.spinand.fs",
            "bsp.d213.ota",
            "aic_ota_slot_status",
            "nand_lfs_bbt_test",
            "nand_lfs_stress",
            "nand_scratch",
            "nand_rodata",
            "bsp.d213.tsen",
            "temp_tsen_cpu",
            "aicupg",
        ],
    },
    "d213-nand-readonly": {
        "bsp": D213_BSP,
        "config": "configs/d213-nand-readonly.config",
        "xlen": 64,
        "intc": "plic",
        "package": True,
        "require_symbols": [
            "rt_hw_qspi0_init",
            "aic_spinand_ro_read_page",
            "aic_spinand_ro_check_block",
            "rt_mtd_nand_register_device",
        ],
        "require_strings": [
            "read-only (program/erase/mark rejected)",
        ],
    },
    "d213-sdcard": {
        "bsp": D213_BSP,
        "config": "configs/d213-sdcard.config",
        "xlen": 64,
        "intc": "plic",
        "package": True,
        "require_symbols": [
            "mmcsd_alloc_host",
            "rt_hw_blk_disk_register",
            "dfs_mount",
            "hal_sdmc_idma_prepare",
            "aic_dma_alloc",
        ],
        "require_strings": [
            "SDIO IRQ armable",
            "FAT filesystem mounted",
        ],
    },
    "d213-sdcard-utest": {
        "bsp": D213_BSP,
        "config": "configs/d213-sdcard-utest.config",
        "xlen": 64,
        "intc": "plic",
        "package": True,
        "require_symbols": [
            "utest_init",
            "mmcsd_alloc_host",
            "rt_hw_blk_disk_register",
            "hal_sdmc_idma_prepare",
            "aic_dma_alloc",
        ],
        "require_symbol_counts": [
            "_utest_testcase=10",
        ],
        "require_strings": [
            "bsp.d213.sdcard",
            "SDIO IRQ armable",
            "bsp.d213.spinand.ro",
        ],
    },
    "d213-sdio-utest": {
        "bsp": D213_BSP,
        "config": "configs/d213-sdio-utest.config",
        "xlen": 64,
        "intc": "plic",
        "package": True,
        "require_symbols": [
            "utest_init",
            "mmcsd_alloc_host",
            "hal_sdmc_idma_prepare",
            "aic_dma_alloc",
            "sdio_attach_irq",
            "sdio_io_rw_extended",
        ],
        "require_symbol_counts": [
            "_utest_testcase=10",
        ],
        "require_strings": [
            "bsp.d213.sdio",
            "bsp.d213.uart7",
            "SDIO IRQ armable",
            "WL_REG_ON",
        ],
    },
    "d213-audio-utest": {
        "bsp": D213_BSP,
        "config": "configs/d213-audio-utest.config",
        "xlen": 64,
        "intc": "plic",
        "package": True,
        "require_symbols": [
            "utest_init",
            "rt_audio_register",
            "rt_audio_rx_done",
            "hal_audio_playback_start",
            "hal_audio_dmic_start",
            "hal_dma_chan_prep_cyclic",
            "hal_request_dma_chan",
            "aic_audio_period_count",
            "aic_dmic_period_count",
        ],
        "require_symbol_counts": [
            "_utest_testcase=9",
        ],
        "require_strings": [
            "bsp.d213.audio",
            "bsp.d213.dmic",
            "sound0 ready",
            "dmic0 ready",
            "audio_tone",
            "dmic_record",
            "audio: no DMA channel",
        ],
    },
    "d213-usb-utest": {
        "bsp": D213_BSP,
        "config": "configs/d213-usb-utest.config",
        "xlen": 64,
        "intc": "plic",
        "package": True,
        "require_symbols": [
            "utest_init",
            "usbh_initialize",
            "usb_hc_low_level_init",
            "usbh_msc_scsi_read10",
            "usbh_msc_run",
            "ohci_init",
            "hal_syscfg_usb_phy1_init",
            "usb_dcache_clean",
        ],
        "require_symbol_counts": [
            "_utest_testcase=11",
        ],
        "require_strings": [
            "bsp.d213.usb",
            "usb: USB1 host PHY ready",
            "udisk: %s mount",
            "EHCI uses companion controller",
        ],
    },
    "d213-usbdev-utest": {
        "bsp": D213_BSP,
        "config": "configs/d213-usbdev-utest.config",
        "xlen": 64,
        "intc": "plic",
        "package": True,
        "require_symbols": [
            "utest_init",
            "usbd_initialize",
            "usbd_cdc_acm_serial_init",
            "usb_dc_low_level_init",
            "USBD_IRQHandler",
            "hal_syscfg_usb_phy0_sw_host",
        ],
        "require_symbol_counts": [
            "_utest_testcase=12",
        ],
        "require_strings": [
            "bsp.d213.usbdev",
            "usb_dev_status",
            "usb_dev_console",
            "usb-acm%d",
        ],
    },
    # Minimal firmware with only the USB0 device function enabled.  This
    # keeps the device-only link path covered independently of USB host,
    # display, GE and GMAC.
    "d213-usbdev-only": {
        "bsp": D213_BSP,
        "config": "configs/d213-usbdev-only.config",
        "xlen": 64,
        "intc": "plic",
        "package": True,
        "require_symbols": [
            "usbd_initialize",
            "usbd_cdc_acm_serial_init",
            "usb_dc_low_level_init",
            "USBD_IRQHandler",
            "hal_syscfg_usb_phy0_sw_host",
            "hal_reset_deassert",
            "hal_clk_enable",
        ],
        "require_strings": [
            "usb_dev_console",
            "usb-acm%d",
        ],
    },
    "d213-ce-utest": {
        "bsp": D213_BSP,
        "config": "configs/d213-ce-utest.config",
        "xlen": 64,
        "intc": "plic",
        "package": True,
        "require_symbols": [
            "utest_init",
            "aic_hw_crypto_device_init",
            "hal_crypto_init",
            "hal_crypto_start_symm",
            "hal_crypto_start_hash",
            "drv_sha_start",
            "drv_sha_finish",
            "rt_hwcrypto_register",
            "rt_hwcrypto_hash_finish",
        ],
        "require_symbol_counts": [
            "_utest_testcase=23",
        ],
        "require_strings": [
            "bsp.d213.ce",
            "hwcrypto device %s",
            "AES run timeout.",
            "SHA run timeout.",
        ],
    },
    "d213-eth-utest": {
        "bsp": D213_BSP,
        "config": "configs/d213-eth-utest.config",
        "xlen": 64,
        "intc": "plic",
        "package": True,
        "require_symbols": [
            "utest_init",
            "eth_device_init",
            "netif_find",
            "dhcp_start",
            "aicmac_init",
            "aicmac_submit_tx_frame",
            "aicphy_read_status",
            "hal_syscfg_gmac0_init",
        ],
        "require_symbol_counts": [
            "_utest_testcase=11",
        ],
        "require_strings": [
            "bsp.d213.eth",
            "eth: e0 link up, ip %s mask %s gw %s",
            "eth_phy_poll",
        ],
    },
    "d126-minimal": {
        "bsp": os.path.join(ARTICHIP_DIR, "d126fxr-demo-v1-1"),
        "config": "configs/d126-minimal.config",
        "xlen": 32,
        "intc": "clic",
        "package": True,
    },
    "d126-utest": {
        "bsp": os.path.join(ARTICHIP_DIR, "d126fxr-demo-v1-1"),
        "config": "configs/d126-utest.config",
        "xlen": 32,
        "intc": "clic",
        "package": True,
        "require_symbols": [
            "utest_init",
            "utest_testcase_run",
            "__rt_utest_tc_tab_start",
            "__rt_utest_tc_tab_end",
            "d126_test_tick",
            "d126_test_sched",
            "d126_test_ipc",
            "rt_hw_serial_isr",
        ],
        "require_strings": [
            "components.utilities.utest.uassert",
            "d126_info: board=",
            "bsp.d126.tick",
            "bsp.d126.scheduler",
            "bsp.d126.ipc",
            "bsp.d126.memory",
            "bsp.d126.gpio",
            "aicupg",
            "reboot: WRI reason",
            "build_id:",
        ],
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


def resolve_toolchain(sdk, explicit):
    """The upstream building.py requires rtconfig.EXEC_PATH to exist."""
    if explicit:
        return explicit
    env = os.getenv("RTT_EXEC_PATH")
    if env:
        return env
    if sdk:
        candidate = os.path.join(sdk, "toolchain", "bin")
        if os.path.isdir(candidate):
            return candidate
    prefix = os.getenv("RTT_CC_PREFIX", "riscv64-unknown-elf-")
    gcc = shutil.which(prefix + "gcc")
    if gcc:
        return os.path.dirname(gcc)
    return None


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def parse_build_id(bsp):
    path = os.path.join(bsp, "build", "build_id_gen.h")
    fields = {}
    if not os.path.isfile(path):
        return fields
    pattern = re.compile(r'#define\s+(AIC_BUILD_\w+)\s+"(.*)"')
    with open(path, encoding="utf-8") as f:
        for line in f:
            match = pattern.match(line.strip())
            if match:
                fields[match.group(1)] = match.group(2)
    return fields


def file_entry(path):
    if not os.path.isfile(path):
        return None
    return {
        "path": path,
        "size": os.path.getsize(path),
        "sha256": sha256(path),
    }


def write_target_manifest(target, bsp, outdir, pkg):
    """Machine-readable build identity for the HIL (never mtime based)."""
    fields = parse_build_id(bsp)
    images = sorted(glob.glob(os.path.join(outdir, "images", "*.img")))
    itbs = sorted(glob.glob(os.path.join(outdir, "images", "*_os.itb")))
    data = {
        "target": target,
        "board": fields.get(
            "AIC_BUILD_TARGET",
            "d213ecv-demo-v4-0" if target.startswith("d213")
            else "d126fxr-demo-v1-1"),
        "build_id": fields.get("AIC_BUILD_ID"),
        "git_sha": fields.get("AIC_BUILD_GIT_SHA"),
        "config_hash": fields.get("AIC_BUILD_CONFIG_HASH"),
        "built_at": fields.get("AIC_BUILD_TIME"),
        "package": pkg,
        "elf": file_entry(os.path.join(outdir, "rtthread.elf")),
        "itb": file_entry(itbs[0]) if itbs else None,
        "img": file_entry(images[0]) if images else None,
    }
    path = os.path.join(outdir, "manifest.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
        f.write("\n")
    return path


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
    if os.path.normcase(os.path.abspath(bsp)) == os.path.normcase(D213_BSP):
        script = os.path.join(ARTICHIP_DIR, "tools", "d213_mk_firmware.py")
    else:
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
    # D126 packaging requires the bootloader built by the same Luban-Lite
    # tree.  mk_firmware.py finds output/d12p_d126-demo124-nor_*/images/
    # bootloader.bin; if it is missing the target FAILs instead of shipping
    # a placeholder image that cannot boot.
    ret = run(cmd, cwd=bsp, log=log, check=False)
    return "PASS" if ret == 0 else "FAIL"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--targets", nargs="*", default=list(TARGETS),
                        choices=list(TARGETS))
    parser.add_argument("--sdk", default=os.getenv("AIC_SDK_DIR"))
    parser.add_argument("--toolchain", default=None,
                        help="RISC-V toolchain bin directory (default: "
                             "RTT_EXEC_PATH, <sdk>/toolchain/bin or PATH)")
    parser.add_argument("--jobs", default="8")
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()

    if not os.path.isfile(CHECKER):
        sys.exit("missing shared checker: " + CHECKER)

    toolchain = resolve_toolchain(args.sdk, args.toolchain)
    if not toolchain:
        sys.exit("no RISC-V toolchain found; pass --toolchain or set "
                 "RTT_EXEC_PATH")
    os.environ["RTT_EXEC_PATH"] = toolchain

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

            check_cmd = [
                sys.executable, CHECKER,
                "--elf", os.path.join(bsp, "rtthread.elf"),
                "--map", os.path.join(bsp, "rtthread.map"),
                "--xlen", str(info["xlen"]),
                "--entry", "0x40000000",
                "--intc", info["intc"],
            ]
            for symbol in info.get("require_symbols", []):
                check_cmd += ["--require-symbol", symbol]
            for spec in info.get("require_symbol_counts", []):
                check_cmd += ["--require-symbol-count", spec]
            for value in info.get("require_strings", []):
                check_cmd += ["--require-string", value]

            check = run(check_cmd, cwd=bsp, log=log, check=False)
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
            write_target_manifest(name, bsp, outdir, pkg)
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
    write_sha256sums()
    return 0 if all(r[1] == "PASS" for r in results) else 1


def write_sha256sums():
    path = os.path.join(ARTIFACTS, "SHA256SUMS")
    entries = []
    for root, dirs, files in os.walk(ARTIFACTS):
        dirs.sort()
        for fn in sorted(files):
            if fn in ("SHA256SUMS", "manifest.md") or fn.endswith(".log"):
                continue
            full = os.path.join(root, fn)
            rel = os.path.relpath(full, ARTIFACTS)
            # HIL run directories hold logs/reports, not build artifacts;
            # the HIL also keeps its build.log open while this runs.
            if rel.startswith(("d213-hil" + os.sep, "d126-hil" + os.sep)):
                continue
            try:
                digest = sha256(full)
            except OSError as exc:
                print("[skip] %s: %s" % (rel, exc))
                continue
            entries.append("%s  %s\n" % (digest, rel))
    with open(path, "w", encoding="utf-8") as f:
        f.writelines(entries)


def write_manifest(results, stamp, sdk):
    path = os.path.join(ARTIFACTS, "manifest.md")
    try:
        head = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=RTT_ROOT, text=True).strip()
    except Exception:
        head = "unknown"
    try:
        upstream = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=RTT_ROOT, text=True).strip()
    except Exception:
        upstream = "unknown"
    for ref in ("origin/master", "upstream/master"):
        try:
            upstream = subprocess.check_output(
                ["git", "rev-parse", ref], cwd=RTT_ROOT, text=True,
                stderr=subprocess.DEVNULL).strip()
            break
        except Exception:
            continue

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
        f.write("- d213-utest: separate test firmware; the ELF is checked "
                "for utest symbols and expected testcase literals.  A "
                "successful build is not a hardware test.\n")
        f.write("- d126-minimal: real packaging requires a D126 "
                "d126-demo124-nor bootloader.  Placeholder images are "
                "renamed *_PLACEHOLDER_DO_NOT_FLASH.img and are not "
                "flashable.\n")
        f.write("- d126-utest: D126 non-FPU utest firmware (tick, "
                "scheduler, IPC, heap).  FPU thread-context tests are not "
                "built into this profile.  A build is not a hardware "
                "test.\n")
        f.write("- HW verification: PENDING.  BUILD/STATIC/PACKAGE PASS "
                "is not hardware validation.\n")


if __name__ == "__main__":
    sys.exit(main())
