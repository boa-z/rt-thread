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
import glob
import hashlib
import json
import os
import re
import subprocess
import sys
import time

try:
    import serial
except ImportError:
    serial = None

ARTICHIP_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RTT_ROOT = os.path.normpath(os.path.join(ARTICHIP_DIR, "..", ".."))
ARTIFACTS = os.path.join(RTT_ROOT, "artifacts")
BUILD_CHECK = os.path.join(ARTICHIP_DIR, "tools", "build-check.py")
UTEST_RUNNER = os.path.join(ARTICHIP_DIR, "tools", "d213_utest.py")

TARGET = "d213-utest"
BOARD = "d213-demo128"
PRODUCT = "d213_demo128_page_2k_block_128k"
DEFAULT_UPGCMD = r"C:\Program Files\AiBurn\upgcmd.exe"
DEFAULT_PORT = os.getenv("D213_PORT", "COM9")
MAX_FLASH_ATTEMPTS = 3

BOOT_MARKERS = ("RT-Thread Operating System", "ArtInChip D213 demo128 RT-Thread BSP")
PROMPT = "msh >"
UPGRADE_MARKERS = ("Enter upgrade mode", "Enter USB upgrade mode")


def _safe_stdout():
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(errors="replace")
        except Exception:
            pass


def stamp():
    return datetime.datetime.now().strftime("%Y%m%d-%H%M%S")


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def git(*args):
    try:
        return subprocess.check_output(
            ["git"] + list(args), cwd=RTT_ROOT, text=True,
            stderr=subprocess.DEVNULL).strip()
    except Exception:
        return "unknown"


def run_logged(cmd, log_path, timeout, cwd=None, env=None):
    with open(log_path, "w", encoding="utf-8", errors="replace") as log:
        log.write("+ " + " ".join(cmd) + "\n")
        log.flush()
        try:
            proc = subprocess.run(cmd, cwd=cwd, env=env, stdout=log,
                                  stderr=subprocess.STDOUT, timeout=timeout)
            return proc.returncode, None
        except subprocess.TimeoutExpired:
            log.write("\n[timeout after %s s]\n" % timeout)
            return None, "timeout"


def capture(cmd, timeout=60):
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True,
                              timeout=timeout)
        return proc.returncode, (proc.stdout or "") + (proc.stderr or "")
    except subprocess.TimeoutExpired:
        return None, "timeout"
    except Exception as exc:
        return None, "failed to run %s: %s" % (cmd[0], exc)


def parse_devices(text):
    stages = re.findall(r"Boot stage:\s*(.+)", text)
    if stages:
        return [s.strip() for s in stages]
    return []


def upgcmd_devices(upgcmd, timeout=30):
    rc, text = capture([upgcmd, "-l"], timeout)
    return parse_devices(text), text


def parse_image(upgcmd, image):
    rc, text = capture([upgcmd, "-i", image], 60)
    product = re.search(r"product\s*:\s*(\S+)", text)
    components = re.findall(r"name\s*:\s*(image\.target\.\S+)", text)
    return {
        "product": product.group(1) if product else None,
        "components": components,
        "raw": text,
    }


class Console:
    def __init__(self, port, baud, log_path):
        self.port = port
        self.baud = baud
        self.log_path = log_path
        self.ser = None
        self.buffer = ""
        self.log = None

    def open(self):
        if serial is None:
            raise RuntimeError("pyserial is required: pip install pyserial")
        self.log = open(self.log_path, "a", encoding="utf-8",
                        errors="replace")
        self.log.write("# serial %s @ %d, %s\n" %
                       (self.port, self.baud,
                        datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")))
        self.log.flush()
        self.ser = serial.Serial(self.port, self.baud, timeout=0.1,
                                 write_timeout=1)

    def close(self):
        try:
            if self.ser is not None:
                self.ser.close()
        finally:
            if self.log is not None:
                self.log.close()
        self.ser = None

    def read_once(self):
        try:
            data = self.ser.read(4096)
        except Exception:
            return ""
        if not data:
            return ""
        text = data.decode("utf-8", "replace")
        self.buffer += text
        self.log.write(text)
        self.log.flush()
        try:
            sys.stdout.write(text)
            sys.stdout.flush()
        except UnicodeEncodeError:
            sys.stdout.write(text.encode("ascii", "replace").decode("ascii"))
            sys.stdout.flush()
        return text

    def drain(self, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            self.read_once()

    def read_until(self, needles, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.read_once()
            for needle in needles:
                if needle in self.buffer:
                    return needle
        return None

    def send(self, line):
        self.ser.write((line + "\r\n").encode("utf-8"))
        self.ser.flush()

    def wait_prompt(self, timeout):
        self.buffer = ""
        deadline = time.monotonic() + timeout
        nudged = False
        while time.monotonic() < deadline:
            self.read_once()
            if PROMPT in self.buffer:
                return True
            if not nudged and time.monotonic() - (deadline - timeout) > 2.0:
                self.send("")
                nudged = True
        return False


def now_iso():
    return datetime.datetime.now().astimezone().isoformat(timespec="seconds")


def write_report(report, report_dir):
    os.makedirs(report_dir, exist_ok=True)
    path = os.path.join(report_dir, "report.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
        f.write("\n")
    return path


def stage(report, name, status, **extra):
    entry = {"status": status}
    entry.update(extra)
    report["stages"][name] = entry
    print("[hil] %-14s %s" % (name, status))
    return entry


def find_image():
    pattern = os.path.join(ARTIFACTS, TARGET, "images",
                           "d21x_d213_demo128_*.img")
    matches = glob.glob(pattern)
    if not matches:
        return None
    return max(matches, key=os.path.getmtime)


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
        stages, text = upgcmd_devices(args.upgcmd)
        last = text
        if stages:
            stage(report, "enter_upgrade", "PASS", device=stages[0])
            return True, stages[0]
        time.sleep(0.5)

    stage(report, "enter_upgrade", "DEVICE_NOT_FOUND")
    return False, "upgcmd did not see the device"


def wait_boot(args, report, report_dir):
    console = Console(args.port, args.baud,
                      os.path.join(report_dir, "serial.log"))
    start = time.monotonic()
    try:
        console.open()
    except Exception as exc:
        stage(report, "boot", "SERIAL_BUSY", error=str(exc))
        return False, "serial busy: %s" % exc

    deadline = start + args.boot_timeout
    try:
        while time.monotonic() < deadline:
            console.read_once()
            if any(m in console.buffer for m in UPGRADE_MARKERS):
                stage(report, "boot", "BOOT_FAIL", reason="entered upgrade mode")
                return False, "board re-entered upgrade mode"
            if PROMPT in console.buffer and any(
                    m in console.buffer for m in BOOT_MARKERS):
                seconds = time.monotonic() - start
                stage(report, "boot", "PASS", seconds=round(seconds, 2))
                return True, "msh prompt"
    except Exception as exc:
        stage(report, "boot", "SERIAL_ERROR", error=str(exc))
        return False, "serial error: %s" % exc
    finally:
        console.close()

    stage(report, "boot", "BOOT_TIMEOUT")
    return False, "RT-Thread prompt not seen in %ss" % args.boot_timeout


def verify_identity(args, report, report_dir):
    """Safe probe: the flashed image must expose the aicupg command."""
    console = Console(args.port, args.baud,
                      os.path.join(report_dir, "serial.log"))
    try:
        console.open()
    except Exception as exc:
        stage(report, "identity", "SERIAL_BUSY", error=str(exc))
        return False, "serial busy: %s" % exc

    try:
        if not console.wait_prompt(10):
            stage(report, "identity", "NO_PROMPT")
            return False, "MSH prompt not found"
        console.buffer = ""
        console.send("aicupg foo")
        if console.read_until(("Usage: aicupg [gotobl]",), 5):
            stage(report, "identity", "PASS")
            return True, "aicupg command present"
        stage(report, "identity", "MISMATCH")
        return False, "running firmware does not expose aicupg"
    except Exception as exc:
        stage(report, "identity", "SERIAL_ERROR", error=str(exc))
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
    parser.add_argument("--image", default=None,
                        help="image path (default: newest artifacts image)")
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--build-timeout", type=int, default=1800)
    parser.add_argument("--prompt-timeout", type=int, default=20)
    parser.add_argument("--upgrade-timeout", type=int, default=30)
    parser.add_argument("--flash-timeout", type=int, default=300)
    parser.add_argument("--boot-timeout", type=int, default=60)
    parser.add_argument("--utest-timeout", type=int, default=180)
    parser.add_argument("--flash-attempts", type=int, default=2,
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
    parser.add_argument("--report-dir", default=None)
    args = parser.parse_args()

    report_dir = args.report_dir or os.path.join(
        ARTIFACTS, "d213-hil", stamp())
    os.makedirs(report_dir, exist_ok=True)

    report = {
        "tool": "d213_hil",
        "schema": 1,
        "started": now_iso(),
        "finished": None,
        "duration_s": None,
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
        "stages": {},
        "logs": {"dir": report_dir},
    }
    started = time.monotonic()

    def finish(status, error=None, code=1):
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
    if args.skip_build:
        stage(report, "build", "SKIP")
    else:
        if not args.sdk:
            return finish("BUILD_FAIL", "missing --sdk (or AIC_SDK_DIR)")
        env = os.environ.copy()
        if args.toolchain:
            env["RTT_EXEC_PATH"] = args.toolchain
        rc, timeout = run_logged(
            [sys.executable, BUILD_CHECK, "--targets", TARGET,
             "--sdk", args.sdk],
            os.path.join(report_dir, "build.log"), args.build_timeout,
            env=env)
        if timeout:
            return finish("BUILD_FAIL", "build timed out")
        if rc != 0:
            return finish("BUILD_FAIL", "build/check/package failed")
        stage(report, "build", "PASS",
              log=os.path.join(report_dir, "build.log"))

    image = args.image or find_image()
    if not image or not os.path.isfile(image):
        stage(report, "image", "IMAGE_MISSING")
        return finish("IMAGE_MISSING", "no packaged image found")

    info = parse_image(args.upgcmd, image)
    report["image"] = {
        "path": image,
        "sha256": sha256_file(image),
        "size": os.path.getsize(image),
        "product": info["product"],
        "components": info["components"],
    }
    if info["product"] != PRODUCT:
        stage(report, "image", "IMAGE_MISMATCH", product=info["product"])
        return finish("IMAGE_MISMATCH",
                      "image product %r does not match %r" %
                      (info["product"], PRODUCT))
    if args.flash_scope == "os" and "image.target.os" not in info["components"]:
        return finish("IMAGE_MISMATCH", "image has no image.target.os")
    stage(report, "image", "PASS", sha256=report["image"]["sha256"])

    # --- device discovery -------------------------------------------------
    stages, text = upgcmd_devices(args.upgcmd)
    with open(os.path.join(report_dir, "upgcmd-list-initial.log"), "w",
              encoding="utf-8") as f:
        f.write(text)
    stage(report, "device", "PASS" if stages else "NOT_FOUND",
          devices=stages)

    if args.mode == "plan":
        return finish("DRY_RUN", None, 0)

    # --- enter upgrade mode ----------------------------------------------
    if not stages:
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
    for attempt in range(1, attempts + 1):
        cmd = [args.upgcmd, "-p"]
        if args.flash_transport == "uart":
            cmd += ["-u", args.port]
        cmd += ["image", image]
        if args.flash_scope == "os":
            cmd.append("image.target.os")
        rc, timeout = run_logged(cmd, flash_log, args.flash_timeout)
        if timeout:
            last_error = "flash timed out"
        elif rc != 0:
            last_error = "upgcmd exited %d" % rc
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
        return finish("FLASH_FAIL", last_error)

    stage(report, "flash", "PASS", attempts=attempt,
          scope=args.flash_scope, log=flash_log)

    # --- boot -------------------------------------------------------------
    if args.flash_transport == "usb":
        stages, _ = upgcmd_devices(args.upgcmd)
        if stages and args.upgrade_target == "brom":
            capture([args.upgcmd, "continue"], 30)
    ok, detail = wait_boot(args, report, report_dir)
    if not ok:
        return finish("BOOT_FAIL", detail)

    # A/B slot fallback could boot the previous image; probe the command.
    ok, detail = verify_identity(args, report, report_dir)
    if not ok:
        return finish("BOOT_FAIL", detail)

    # --- utest ------------------------------------------------------------
    result = run_utest(args, report, report_dir)
    if result != "PASS":
        return finish(result,
                      "utest finished with %s" % result, 0 if result ==
                      "TEST_KNOWN_FAIL" else 1)

    return finish("PASS", None, 0)


if __name__ == "__main__":
    sys.exit(main())
