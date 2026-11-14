#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# SPDX-License-Identifier: Apache-2.0
#
# D213 hardware-in-the-loop runner.
#
# Pipeline (--mode full):
#   build + static check + package -> enter upgrade mode -> upgcmd flash
#   -> wait for RT-Thread -> run d213_utest.py -> JSON report
#
# Safety:
#   * --mode plan (default) never changes the board state.
#   * --mode enter-upgrade sends "aicupg" or "aicupg gotobl" over the
#     console and verifies that upgcmd can see the BROM/Bootloader device.
#     It never writes flash.
#   * --mode full requires --confirm-board d213-demo128 before anything is
#     done.  The default flash scope is "os": only image.target.os is
#     written, so the existing SPL, Bootloader, env and data partitions are
#     preserved.  "--flash-scope full" additionally requires
#     --confirm-full-flash.
#   * Flash attempts are bounded (default 2, hard cap 3).  A failed utest run
#     never triggers an automatic reflash.
#   * flasherase / eFuse / security commands are never issued.

import argparse
import datetime
import json
import os
import re
import subprocess
import sys
import time

import hil_common as common

# Re-exported so callers and the host tests can keep using d213_hil.<name>.
from hil_common import (  # noqa: F401
    Console,
    FileLock,
    aicflash_scan,
    capture,
    classify_flash_failure,
    make_os_only_image,
    now_iso,
    parse_build_id_output,
    parse_devices,
    parse_scan_devices,
    read_text,
    sha256_file,
    stage,
    stamp,
    upgcmd_devices,
    write_report,
)

ARTICHIP_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RTT_ROOT = os.path.normpath(os.path.join(ARTICHIP_DIR, "..", ".."))
ARTIFACTS = os.path.join(ARTICHIP_DIR, "artifacts")
BUILD_CHECK = os.path.join(ARTICHIP_DIR, "tools", "build-check.py")
UTEST_RUNNER = os.path.join(ARTICHIP_DIR, "tools", "d213_utest.py")

TARGET = "d213-utest"
BOARD = "d213-demo128"
PRODUCT = "d213_demo128_page_2k_block_128k"
DEFAULT_UPGCMD = r"C:\Program Files\AiBurn\upgcmd.exe"
DEFAULT_AICFLASH = r"C:\Program Files (x86)\artinchip-flash\artinchip-flash.exe"
DEFAULT_PORT = os.getenv("D213_PORT", "COM9")
MAX_FLASH_ATTEMPTS = 3

BOOT_MARKERS = ("RT-Thread Operating System", "ArtInChip D213 demo128 RT-Thread BSP")
PROMPT = "msh >"
# With DFS enabled FinSH prints the current directory, e.g. "msh />" or
# "msh /sdcard>".  Accept that form too so USB/DFS test images are not
# mistaken for a boot failure.
PROMPT_RE = re.compile(r"msh\s+(?:[^>\s]+\s*)?>")
UPGRADE_MARKERS = ("Enter upgrade mode", "Enter USB upgrade mode")


def _safe_stdout():
    common._safe_stdout()


def git(*args):
    return common.git(RTT_ROOT, *args)


def source_state():
    return common.source_state(RTT_ROOT)


def run_logged(*args, **kwargs):
    return common.run_logged(*args, **kwargs)


def open_console(args, log_path, timeout=15):
    return common.open_console(args.port, args.baud, log_path, timeout)


def flasher_exe(args):
    return args.flasher_path or DEFAULT_AICFLASH


def parse_image(upgcmd, image):
    rc, text = capture([upgcmd, "-i", image], 60)
    product = re.search(r"product\s*:\s*(\S+)", text)
    components = re.findall(r"name\s*:\s*(image\.target\.\S+)", text)
    return {
        "product": product.group(1) if product else None,
        "components": components,
        "raw": text,
    }


def parse_image_aicflash(flasher, image):
    """Parse the image locally with the flasher; no USB access."""
    rc, text = capture([flasher, "info", image], 60)
    product = re.search(r"Product:\s*(\S+)", text)
    components = re.findall(r"\]\s+(image\.target\.\S+)\s", text)
    return {
        "product": product.group(1) if product else None,
        "components": components,
        "raw": text,
    }


def lock_path(name):
    return os.path.join(ARTIFACTS, ".locks", name)


def load_manifest(target):
    return common.load_manifest(ARTIFACTS, target)


def resolve_image(args):
    """Select the image from the build manifest, never from mtime."""
    return common.resolve_image(args, ARTIFACTS)


def enter_upgrade(args, report, report_dir):
    """Send aicupg over the console and wait for upgcmd to see the device."""
    cmd = "aicupg" if args.upgrade_target == "brom" else "aicupg gotobl"
    console = Console(args.port, args.baud,
                      os.path.join(report_dir, "serial.log"))
    try:
        console.open()
    except Exception as exc:
        stage(report, "enter_upgrade", "SERIAL_BUSY", error=str(exc))
        return False, "serial busy: %s" % exc

    try:
        if not console.wait_prompt(args.prompt_timeout):
            if any(m in console.buffer for m in UPGRADE_MARKERS):
                return True, "already in upgrade mode"
            stage(report, "enter_upgrade", "NO_PROMPT")
            return False, "MSH prompt not found"

        console.buffer = ""
        console.send(cmd)
        if not console.read_until(("latched and verified",), 10):
            stage(report, "enter_upgrade", "WRITE_FAIL")
            return False, "reboot reason was not latched"
        console.drain(0.5)
    except Exception as exc:
        stage(report, "enter_upgrade", "SERIAL_ERROR", error=str(exc))
        return False, "serial error: %s" % exc
    finally:
        console.close()

    if args.flash_transport == "uart":
        time.sleep(3)
        return True, "uart upgrade requested"

    deadline = time.monotonic() + args.upgrade_timeout
    last = ""
    while time.monotonic() < deadline:
        if args.flasher == "artinchip-flash":
            # upgcmd -l can time out and leave the BROM session wedged;
            # the flasher's own scan is the safe discovery path.
            paths, _ = aicflash_scan(flasher_exe(args), 10)
            if paths:
                stage(report, "enter_upgrade", "PASS",
                      device="USB path %s" % paths[0])
                return True, "USB path %s" % paths[0]
        else:
            stages, text = upgcmd_devices(args.upgcmd, 8)
            last = text
            if stages:
                stage(report, "enter_upgrade", "PASS", device=stages[0])
                return True, stages[0]
        time.sleep(0.5)

    stage(report, "enter_upgrade", "DEVICE_NOT_FOUND")
    return False, "no upgrade device appeared"


def wait_boot(args, report, report_dir):
    start = time.monotonic()
    try:
        console = open_console(args, os.path.join(report_dir, "serial.log"))
    except Exception as exc:
        stage(report, "boot", "SERIAL_BUSY", error=str(exc))
        return False, "serial busy: %s" % exc

    try:
        if console.wait_prompt(args.boot_timeout):
            seconds = time.monotonic() - start
            stage(report, "boot", "PASS", seconds=round(seconds, 2))
            return True, "msh prompt"
        if any(m in console.buffer for m in UPGRADE_MARKERS):
            stage(report, "boot", "BOOT_FAIL", reason="entered upgrade mode")
            return False, "board re-entered upgrade mode"
    except Exception as exc:
        stage(report, "boot", "SERIAL_ERROR", error=str(exc))
        return False, "serial error: %s" % exc
    finally:
        console.close()

    stage(report, "boot", "BOOT_TIMEOUT")
    return False, "RT-Thread prompt not seen in %ss" % args.boot_timeout


def verify_build_id(args, report, report_dir, expected):
    """The running firmware must report the exact manifest Build ID."""
    try:
        console = open_console(args, os.path.join(report_dir, "serial.log"))
    except Exception as exc:
        stage(report, "build_id", "SERIAL_BUSY", error=str(exc))
        return False, "serial busy: %s" % exc

    try:
        if not console.wait_prompt(10):
            stage(report, "build_id", "NO_PROMPT")
            return False, "MSH prompt not found"
        console.buffer = ""
        console.send("aic_build_id")
        if not console.read_until(("build_id:",), 5):
            stage(report, "build_id", "BUILD_ID_MISMATCH",
                  reason="aic_build_id unavailable")
            return False, "running firmware has no build_id"
        console.drain(0.2)
        actual = parse_build_id_output(console.buffer)
        if not expected:
            stage(report, "build_id", "BUILD_ID_MISMATCH",
                  reason="manifest has no build_id", actual=actual)
            return False, "manifest has no build_id"
        if actual != expected:
            stage(report, "build_id", "BUILD_ID_MISMATCH",
                  expected=expected, actual=actual)
            return False, "build_id %s != manifest %s" % (actual, expected)
        stage(report, "build_id", "PASS", build_id=actual)
        return True, "build_id matched"
    except Exception as exc:
        stage(report, "build_id", "SERIAL_ERROR", error=str(exc))
        return False, "serial error: %s" % exc
    finally:
        console.close()


def run_utest(args, report, report_dir):
    log_path = os.path.join(report_dir, "utest.log")
    cmd = [sys.executable, UTEST_RUNNER,
           "--port", args.port,
           "--baud", str(args.baud),
           "--log", log_path,
           "--timeout", str(args.utest_timeout)]
    for suite in args.suite or []:
        cmd += ["--suite", suite]
    for known in args.known_fail or []:
        cmd += ["--known-fail", known]
    for pattern in getattr(args, "exclude", None) or []:
        cmd += ["--exclude", pattern]

    suite_count = len(args.suite) if args.suite else 6  # d213_utest defaults
    limit = args.utest_timeout * suite_count + 120
    rc, timeout = run_logged(cmd, os.path.join(report_dir, "utest.out"),
                             limit)
    if timeout:
        stage(report, "utest", "TEST_TIMEOUT", log=log_path)
        return "TEST_TIMEOUT"
    if rc == 0:
        stage(report, "utest", "PASS", log=log_path)
        return "PASS"
    if rc == 2:
        stage(report, "utest", "TEST_KNOWN_FAIL", log=log_path)
        return "TEST_KNOWN_FAIL"
    stage(report, "utest", "TEST_FAIL", exit_code=rc, log=log_path)
    return "TEST_FAIL"


def main():
    _safe_stdout()
    parser = argparse.ArgumentParser(
        description="D213 build/flash/utest hardware-in-the-loop runner.")
    parser.add_argument("--mode", choices=("plan", "enter-upgrade", "full"),
                        default="plan",
                        help="plan (default, no state change), enter-upgrade "
                             "(reboot into upgrade mode, no flash) or full "
                             "(flash + boot + utest)")
    parser.add_argument("--port", default=DEFAULT_PORT,
                        help="serial console (default: %s)" % DEFAULT_PORT)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--sdk", default=os.getenv("AIC_SDK_DIR"),
                        help="Luban-Lite SDK for packaging")
    parser.add_argument("--toolchain", default=os.getenv("RTT_EXEC_PATH"),
                        help="RISC-V toolchain bin directory")
    parser.add_argument("--upgcmd", default=DEFAULT_UPGCMD)
    parser.add_argument("--flasher", choices=("artinchip-flash", "upgcmd"),
                        default="artinchip-flash",
                        help="flash backend (default: artinchip-flash)")
    parser.add_argument("--flasher-path", default=None,
                        help="path to artinchip-flash.exe")
    parser.add_argument("--device", default=None,
                        help="USB selector for artinchip-flash "
                             "(port path 9-2 or bus:address 1:45)")
    parser.add_argument("--image", default=None,
                        help="image path (default: newest artifacts image)")
    parser.add_argument("--target", default=TARGET,
                        help="build-check target/profile (default: %s)" % TARGET)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--build-timeout", type=int, default=1800)
    parser.add_argument("--prompt-timeout", type=int, default=20)
    parser.add_argument("--upgrade-timeout", type=int, default=30)
    parser.add_argument("--flash-timeout", type=int, default=300)
    parser.add_argument("--boot-timeout", type=int, default=60)
    parser.add_argument("--utest-timeout", type=int, default=180)
    parser.add_argument("--flash-attempts", type=int, default=1,
                        help="flash attempts (default 2, cap %d)" %
                             MAX_FLASH_ATTEMPTS)
    parser.add_argument("--upgrade-target", choices=("brom", "bootloader"),
                        default="brom")
    parser.add_argument("--flash-transport", choices=("usb", "uart"),
                        default="usb")
    parser.add_argument("--flash-scope", choices=("os", "full"), default="os",
                        help="os: only image.target.os (default); full: "
                             "every component in the image")
    parser.add_argument("--confirm-board", default=None,
                        help="required by --mode full: %s" % BOARD)
    parser.add_argument("--confirm-full-flash", action="store_true",
                        help="required with --flash-scope full")
    parser.add_argument("--suite", action="append",
                        help="utest suite (repeatable)")
    parser.add_argument("--known-fail", action="append",
                        help="expected failing testcase (repeatable)")
    parser.add_argument("--exclude", action="append",
                        help="skip utest testcase/wildcard (repeatable)")
    parser.add_argument("--report-dir", default=None)
    args = parser.parse_args()

    run_id = stamp() + "-%d" % os.getpid()
    report_dir = args.report_dir or os.path.join(
        ARTIFACTS, "d213-hil", run_id)
    os.makedirs(report_dir, exist_ok=True)

    report = {
        "tool": "d213_hil",
        "schema": 1,
        "started": now_iso(),
        "finished": None,
        "duration_s": None,
        "run_id": run_id,
        "mode": args.mode,
        "status": "RUNNING",
        "error": None,
        "board": args.confirm_board or BOARD,
        "port": args.port,
        "git": {
            "branch": git("rev-parse", "--abbrev-ref", "HEAD"),
            "commit": git("rev-parse", "HEAD"),
            "dirty": bool(git("status", "--porcelain")),
        },
        "image": None,
        "manifest": None,
        "build_id": None,
        "stages": {},
        "logs": {"dir": report_dir},
    }
    started = time.monotonic()

    locks = []

    def finish(status, error=None, code=1):
        for lock in locks:
            lock.release()
        report["status"] = status
        report["error"] = error
        report["finished"] = now_iso()
        report["duration_s"] = round(time.monotonic() - started, 2)
        path = write_report(report, report_dir)
        print("[hil] status: %s" % status)
        if error:
            print("[hil] error : %s" % error)
        print("[hil] report: %s" % path)
        return code

    # --- safety gates -----------------------------------------------------
    if args.mode == "full":
        if args.confirm_board != BOARD:
            return finish("AUTHORIZATION_REQUIRED",
                          "pass --confirm-board %s after checking the board "
                          "model and image" % BOARD, 2)
        if args.flash_scope == "full" and not args.confirm_full_flash:
            return finish("AUTHORIZATION_REQUIRED",
                          "--flash-scope full rewrites SPL/Bootloader/env; "
                          "pass --confirm-full-flash to allow it", 2)
    attempts = max(1, min(args.flash_attempts, MAX_FLASH_ATTEMPTS))

    # --- build + package --------------------------------------------------
    build_lock = FileLock(lock_path("build-%s.lock" % args.target))
    if not build_lock.acquire():
        return finish("RESOURCE_BUSY", "build lock is held")
    locks.append(build_lock)
    before = None if args.skip_build else source_state()
    if args.skip_build:
        stage(report, "build", "SKIP")
    else:
        if not args.sdk:
            return finish("BUILD_FAIL", "missing --sdk (or AIC_SDK_DIR)")
        env = os.environ.copy()
        if args.toolchain:
            env["RTT_EXEC_PATH"] = args.toolchain
        rc, timeout = run_logged(
            [sys.executable, BUILD_CHECK, "--targets", args.target,
             "--sdk", args.sdk],
            os.path.join(report_dir, "build.log"), args.build_timeout,
            env=env)
        if timeout:
            return finish("BUILD_FAIL", "build timed out")
        if rc != 0:
            text = read_text(os.path.join(report_dir, "build.log"))
            if "package=FAIL" in text:
                return finish("PACKAGE_FAIL", "static check passed but packaging failed")
            return finish("BUILD_FAIL", "build/check failed")
        if source_state() != before:
            return finish("PROVENANCE_INDETERMINATE",
                          "source tree changed during the build")
        stage(report, "build", "PASS",
              log=os.path.join(report_dir, "build.log"))

    if args.mode == "enter-upgrade":
        image = None
        build_id = None
        stage(report, "image", "SKIP", reason="enter-upgrade only")
    else:
        image, manifest_sha, build_id, manifest_path, error = resolve_image(args)
        if error:
            stage(report, "image", "IMAGE_MISSING")
            return finish("IMAGE_MISSING", error)
        image_sha = sha256_file(image)
        if manifest_sha and image_sha != manifest_sha:
            stage(report, "image", "IMAGE_MISMATCH")
            return finish("IMAGE_MISMATCH", "image SHA256 does not match manifest")
        if args.flasher == "artinchip-flash":
            info = parse_image_aicflash(flasher_exe(args), image)
        else:
            info = parse_image(args.upgcmd, image)
        report["image"] = {
            "path": image,
            "sha256": image_sha,
            "size": os.path.getsize(image),
            "product": info["product"],
            "components": info["components"],
            "build_id": build_id,
            "manifest": manifest_path,
        }
        report["build_id"] = build_id
        report["manifest"] = manifest_path
        if info["product"] != PRODUCT:
            stage(report, "image", "IMAGE_MISMATCH", product=info["product"])
            return finish("IMAGE_MISMATCH",
                          "image product %r does not match %r" %
                          (info["product"], PRODUCT))
        if args.flash_scope == "os" and "image.target.os" not in info["components"]:
            return finish("IMAGE_MISMATCH", "image has no image.target.os")
        if args.mode == "full" and not build_id:
            return finish("IMAGE_MISMATCH",
                          "manifest has no build_id; rebuild with gen_build_id")
        stage(report, "image", "PASS", sha256=image_sha, build_id=build_id)

    # --- os-only image ----------------------------------------------------
    # Older artinchip-flash builds have no --partition filter.  Rewriting
    # the target section is the only way to keep SPL/env intact; never let
    # an "os" run fall back to the full image.  The D213 flow keeps this
    # path for command/behaviour compatibility.
    if (args.flash_scope == "os" and args.flasher == "artinchip-flash"
            and args.mode == "full"):
        try:
            os_image = make_os_only_image(
                image, report_dir, args.sdk or os.getenv("AIC_SDK_DIR"))
        except Exception as exc:
            stage(report, "os_image", "FAIL", error=str(exc))
            return finish("IMAGE_MISMATCH", "cannot derive os-only image: %s" % exc)
        os_info = parse_image_aicflash(flasher_exe(args), os_image)
        if os_info["components"] != ["image.target.os"]:
            stage(report, "os_image", "IMAGE_MISMATCH",
                  components=os_info["components"])
            return finish("IMAGE_MISMATCH",
                          "derived image targets %r, not only os" %
                          (os_info["components"],))
        image = os_image
        report["flash_image"] = {
            "path": image,
            "sha256": sha256_file(image),
            "components": os_info["components"],
        }
        stage(report, "os_image", "PASS", path=image,
              sha256=report["flash_image"]["sha256"])

    # --- device discovery -------------------------------------------------
    stages = []
    scan_paths = []
    if args.flasher == "artinchip-flash":
        # Never touch upgcmd on this backend: its USB probe can wedge
        # the BROM enumeration.
        scan_paths, text = aicflash_scan(flasher_exe(args), 20)
    else:
        stages, text = upgcmd_devices(args.upgcmd)
    with open(os.path.join(report_dir, "upgcmd-list-initial.log"), "w",
              encoding="utf-8") as f:
        f.write(text)
    stage(report, "device", "PASS" if (stages or scan_paths) else "NOT_FOUND",
          devices=stages, usb_paths=scan_paths)

    if args.mode == "plan":
        return finish("DRY_RUN", None, 0)

    serial_lock = FileLock(
        lock_path("serial-%s.lock" % args.port.replace(":", "_")))
    if not serial_lock.acquire():
        return finish("RESOURCE_BUSY", "serial lock is held")
    locks.append(serial_lock)
    target = args.device or args.port
    flash_lock = FileLock(lock_path("flash-%s.lock" % target.replace(":", "_")))
    if not flash_lock.acquire():
        return finish("RESOURCE_BUSY", "USB target lock is held")
    locks.append(flash_lock)

    # --- enter upgrade mode ----------------------------------------------
    if not stages and not scan_paths:
        ok, detail = enter_upgrade(args, report, report_dir)
        if not ok:
            return finish("UPGRADE_ENTRY_FAIL", detail)
    else:
        stage(report, "enter_upgrade", "SKIP", reason="already in upgrade mode")

    if args.mode == "enter-upgrade":
        return finish("UPGRADE_ENTRY_OK", None, 0)

    # --- flash ------------------------------------------------------------
    if args.flash_transport == "uart":
        if not args.port:
            return finish("FLASH_FAIL", "--flash-transport uart needs --port")
        time.sleep(1)

    flash_log = os.path.join(report_dir, "flash.log")
    last_error = None
    last_status = None
    if args.flasher == "artinchip-flash":
        flasher = flasher_exe(args)
        if not os.path.isfile(flasher):
            return finish("FLASH_FAIL", "flasher not found: %s" % flasher)
    for attempt in range(1, attempts + 1):
        if args.flasher == "artinchip-flash":
            # No --verbose: its console logging perturbs the reconnect timing.
            # --flash-scope os is expressed by the derived image; this
            # flasher version has no --partition option.
            cmd = [flasher, "burn", image]
            if args.upgrade_target == "bootloader":
                cmd += ["--no-updater"]
            if args.device:
                cmd += ["--device", args.device]
        else:
            cmd = [args.upgcmd, "-p"]
            if args.flash_transport == "uart":
                cmd += ["-u", args.port]
            cmd += ["image", image]
            if args.flash_scope == "os":
                cmd.append("image.target.os")
        rc, timeout = run_logged(cmd, flash_log, args.flash_timeout)
        if timeout:
            last_error = "flash timed out"
            last_status = "FLASH_INTERRUPTED"
        elif rc != 0:
            last_status = classify_flash_failure(read_text(flash_log))
            last_error = "%s exited %d" % (args.flasher, rc)
        else:
            last_error = None
            break
        stage(report, "flash", "RETRY" if attempt < attempts else "FLASH_FAIL",
              attempt=attempt, error=last_error)
        if attempt < attempts:
            time.sleep(2)
            if args.flash_transport == "usb":
                stages, _ = upgcmd_devices(args.upgcmd)
                if not stages:
                    ok, _ = enter_upgrade(args, report, report_dir)
                    if not ok:
                        break

    if last_error:
        return finish(last_status or "FLASH_FAIL", last_error)

    stage(report, "flash", "PASS", attempts=attempt,
          flasher=args.flasher, scope=args.flash_scope, log=flash_log)

    # --- boot -------------------------------------------------------------
    if args.flash_transport == "usb" and args.flasher != "artinchip-flash":
        stages, _ = upgcmd_devices(args.upgcmd)
        if stages and args.upgrade_target == "brom":
            capture([args.upgcmd, "continue"], 30)
    ok, detail = wait_boot(args, report, report_dir)
    if not ok:
        return finish("BOOT_FAIL", detail)

    # Reject a board that still runs the previous image or the other slot.
    ok, detail = verify_build_id(args, report, report_dir,
                                 report.get("build_id"))
    if not ok:
        return finish("BUILD_ID_MISMATCH", detail)

    # --- utest ------------------------------------------------------------
    result = run_utest(args, report, report_dir)
    if result != "PASS":
        return finish(result,
                      "utest finished with %s" % result, 0 if result ==
                      "TEST_KNOWN_FAIL" else 1)

    return finish("PASS", None, 0)


if __name__ == "__main__":
    sys.exit(main())
