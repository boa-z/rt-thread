#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# SPDX-License-Identifier: Apache-2.0
#
# ArtInChip D126 (D12p) hardware-in-the-loop runner.
#
# Modes:
#   plan (default)  local safety check only: manifest, image product and
#                   component list.  It never opens the serial port, never
#                   probes USB and never changes the board.
#   serial          M1 read-only automation: connect to the console,
#                   identify the board, run tick/scheduling/IPC/heap tests
#                   repeatedly and write a JSON report.  It never flashes.
#   enter-upgrade   send "aicupg" over the console and wait for the D12p
#                   BROM/Bootloader upgrade device.  No flash write.
#   stability       P1 reset/long-run check: warm-reset the board a bounded
#                   number of times, verify boot banner, Build ID and the
#                   smoke suite after every reset, then optionally keep
#                   exercising tick/scheduling/IPC/heap for a fixed time.
#   full            M2 closed loop: build/static check/package -> derive an
#                   os-only image -> enter upgrade -> artinchip-flash burn
#                   -> boot -> Build ID check -> utest -> report.
#
# Safety:
#   * --mode full requires --confirm-board d126-demo124-nor.
#   * The default flash scope is "os": only image.target.os is written.
#     "--flash-scope full" additionally requires --confirm-full-flash.
#   * Flash attempts are bounded and a failed utest never triggers a
#     reflash.  flasherase/eFuse/security commands are never issued.
#   * Serial and USB target locks keep concurrent agents apart; build
#     provenance is checked around the build.

import argparse
import json
import os
import re
import sys
import time

import hil_common as common

ARTICHIP_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RTT_ROOT = os.path.normpath(os.path.join(ARTICHIP_DIR, "..", ".."))
ARTIFACTS = os.path.join(ARTICHIP_DIR, "artifacts")
BUILD_CHECK = os.path.join(ARTICHIP_DIR, "tools", "build-check.py")
UTEST_RUNNER = os.path.join(ARTICHIP_DIR, "tools", "d126_utest.py")

TARGET = "d126-utest"
BOARD = "d126-demo124-nor"
PRODUCT = "d126-demo124-nor"
OS_COMPONENT = "image.target.os"
DEFAULT_UPGCMD = r"C:\Program Files\AiBurn\upgcmd.exe"
DEFAULT_AICFLASH = r"C:\Program Files (x86)\artinchip-flash\artinchip-flash.exe"
DEFAULT_PORT = os.getenv("D126_PORT", "COM10")
MAX_FLASH_ATTEMPTS = 3
MAX_STABILITY_CYCLES = 20
MAX_STABILITY_SECONDS = 3600

BOOT_MARKERS = ("ArtInChip D126 (D12p) RT-Thread BSP",)
RESET_MARKERS = BOOT_MARKERS
UPGRADE_MARKERS = ("Enter upgrade mode", "Enter USB upgrade mode")
PROMPT = "msh >"

PASS_RE = re.compile(r"\bPASS\b")
FAIL_RE = re.compile(r"\bFAIL\b")
MISSING_CMD_RE = re.compile(
    r"(command not found|symbol not found|Unknown command|"
    r"not a valid command)", re.IGNORECASE)
BUILD_ID_RE = re.compile(r"^build_id:\s*(\S+)\s*$", re.MULTILINE)


def flasher_exe(args):
    return args.flasher_path or DEFAULT_AICFLASH


def lock_path(name):
    return common.lock_path(ARTIFACTS, name)


def load_manifest(target):
    return common.load_manifest(ARTIFACTS, target)


def resolve_image(args):
    return common.resolve_image(args, ARTIFACTS)


def read_text(path):
    return common.read_text(path)


def sha256_file(path):
    return common.sha256_file(path)


def stamp():
    return common.stamp()


def now_iso():
    return common.now_iso()


def git(*args):
    return common.git(RTT_ROOT, *args)


def source_state():
    return common.source_state(RTT_ROOT)


def stage(report, name, status, **extra):
    return common.stage(report, name, status, **extra)


def write_report(report, report_dir):
    return common.write_report(report, report_dir)


def run_logged(*args, **kwargs):
    return common.run_logged(*args, **kwargs)


def open_console(args, log_path, timeout=15):
    return common.open_console(args.port, args.baud, log_path, timeout)


def parse_build_id_output(text):
    return common.parse_build_id_output(text)


def plan_os_partition(flasher, image):
    return common.plan_os_partition(flasher, image)


def selector_known(selector, paths, scan_text):
    """Check an artinchip-flash --device selector against a scan dump.

    The tool accepts both a physical path ("9-1") and bus:address
    ("1:25").  A scan can see unrelated ArtInChip devices, so bootstrap
    needs to know that the explicit selector is real.
    """
    if selector in paths:
        return True
    match = re.match(r"^(\d+):(\d+)$", selector)
    if not match:
        return False
    bus, address = match.groups()
    forward = r"bus[^0-9\n]*%s\b[^\n]*?address[^0-9\n]*%s\b" % (bus, address)
    reverse = r"address[^0-9\n]*%s\b[^\n]*?bus[^0-9\n]*%s\b" % (address, bus)
    return bool(re.search(forward, scan_text) or re.search(reverse, scan_text))


# --------------------------------------------------------------------------
# M1: read-only console tests
# --------------------------------------------------------------------------

def _command_result(console, command, timeout):
    """Run one msh command and stop at the next prompt/reset/timeout."""
    console.buffer = ""
    console.send(command)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        console.read_once()
        if any(marker in console.buffer for marker in RESET_MARKERS):
            return {"status": "RESET", "output": console.buffer,
                    "seconds": round(time.monotonic() - (deadline - timeout), 2)}
        if common.PROMPT_RE.search(console.buffer):
            seconds = time.monotonic() - (deadline - timeout)
            console.drain(0.15)
            return {"status": "RUN", "output": console.buffer,
                    "seconds": round(seconds, 2)}
    return {"status": "TIMEOUT", "output": console.buffer, "seconds": timeout}


def _classify(command, result, pass_re, expect_board=None):
    entry = {
        "command": command,
        "status": result["status"],
        "seconds": result["seconds"],
        "output": result["output"],
    }
    if result["status"] != "RUN":
        return entry

    text = result["output"]
    if MISSING_CMD_RE.search(text):
        entry["status"] = "SKIP"
        entry["reason"] = "command not present in this firmware"
        return entry
    if FAIL_RE.search(text):
        entry["status"] = "FAIL"
        return entry
    if expect_board:
        entry["status"] = "PASS" if re.search(expect_board, text) else "FAIL"
        if entry["status"] == "FAIL":
            entry["reason"] = "unexpected board identity"
        return entry
    if pass_re and pass_re.search(text):
        entry["status"] = "PASS"
        return entry
    entry["status"] = "FAIL"
    entry["reason"] = "no PASS/FAIL marker in output"
    return entry


def board_identity(console):
    """Identify a D126 board without relying on the boot banner.

    Strong: d126_info reports the D126 board name.
    Weak:   the D126-specific d126_memtest command exists (older firmware).
    Unknown: only generic RT-Thread commands answered.
    """
    result = _command_result(console, "d126_info", 5)
    text = result["output"]
    if result["status"] == "RUN":
        match = re.search(r"d126_info: board=(\S+)", text)
        if match:
            return {
                "strength": "strong" if match.group(1) == BOARD else "wrong",
                "board": match.group(1),
                "command": "d126_info",
            }

    result = _command_result(console, "d126_memtest 1", 5)
    if result["status"] == "RUN" and re.search(
            r"memtest:.*\b(?:PASS|FAIL)\b", result["output"]):
        return {"strength": "weak", "board": BOARD,
                "command": "d126_memtest", "note": "pre-d126_info firmware"}

    return {"strength": "unknown", "board": None, "command": None}


def m1_suite(console, repeat, timeout, expect_build_id=None):
    tests = []
    # A TIMEOUT/RESET means the console is gone (assert loop, crash or
    # reboot).  Stop sending commands so the report is produced quickly
    # and the log is not padded with pointless timeouts.
    dead = {"value": False}

    def run(command, overrides=None):
        if dead["value"]:
            return {"status": "SKIP", "output": "", "seconds": 0}
        result = _command_result(console, command, overrides or timeout)
        if result["status"] in ("TIMEOUT", "RESET"):
            dead["value"] = True
        return result

    info = run("d126_info")
    tests.append(_classify("d126_info", info,
                           re.compile(r"d126_info: board=" +
                                      re.escape(BOARD))))

    version = run("version")
    tests.append(_classify(
        "version", version,
        re.compile(r"RT-Thread|Thread Operating System")))

    build = run("aic_build_id")
    build_entry = _classify("aic_build_id", build,
                            re.compile(r"^build_id:", re.MULTILINE))
    build_id = None
    if build_entry["status"] == "PASS":
        build_id = parse_build_id_output(build_entry["output"])
        if not build_id:
            build_entry["status"] = "FAIL"
            build_entry["reason"] = "build_id marker missing"
    tests.append(build_entry)
    if expect_build_id and build_id != expect_build_id:
        build_entry["status"] = "FAIL"
        build_entry["reason"] = "running build_id != expected"

    def repeatable(name, command, pass_re, required):
        iterations = []
        for _ in range(repeat):
            if dead["value"]:
                break
            result = run(command)
            iterations.append(_classify(command, result, pass_re))
        tests.append({
            "command": command,
            "name": name,
            "required": required,
            "iterations": iterations,
        })

    repeatable("tick", "d126_tick 100",
               re.compile(r"d126_tick:.*\bPASS\b"), True)
    repeatable("sched", "d126_sched 500",
               re.compile(r"d126_sched:.*\bPASS\b"), True)
    repeatable("ipc", "d126_ipc",
               re.compile(r"d126_ipc:.*\bPASS\b"), True)
    repeatable("mem", "d126_memtest 1024",
               re.compile(r"memtest:.*\bPASS\b"), True)

    return build_id, tests


def summarize_m1(tests):
    required_failed = []
    incomplete = []
    for test in tests:
        if "iterations" in test:
            statuses = [item["status"] for item in test["iterations"]]
            if any(s in ("FAIL", "TIMEOUT", "RESET") for s in statuses):
                required_failed.append(test["name"])
            elif test["required"] and not all(s == "PASS" for s in statuses):
                incomplete.append(test["name"])
        else:
            if test["status"] in ("FAIL", "TIMEOUT", "RESET"):
                required_failed.append(test["command"])
    if required_failed:
        return "FAIL", required_failed
    if incomplete:
        return "INCOMPLETE", incomplete
    return "PASS", []


def run_serial_mode(args, report, report_dir):
    try:
        console = open_console(args, os.path.join(report_dir, "serial.log"),
                               args.prompt_timeout)
    except Exception as exc:
        stage(report, "serial", "SERIAL_BUSY", error=str(exc))
        return "SERIAL_BUSY", "serial busy: %s" % exc

    try:
        if not console.wait_prompt(args.prompt_timeout):
            stage(report, "serial", "NO_PROMPT")
            return "NO_PROMPT", "MSH prompt not found"

        ident = board_identity(console)
        report["firmware"]["board_identity"] = ident
        stage(report, "identity",
              "PASS" if ident["strength"] in ("strong", "weak") else "UNKNOWN",
              **ident)

        repeat = max(1, min(args.repeat, 10))
        build_id, tests = m1_suite(console, repeat, args.cmd_timeout,
                                   args.expect_build_id)
        report["firmware"]["build_id"] = build_id
        report["tests"] = tests
    except Exception as exc:
        stage(report, "serial", "SERIAL_ERROR", error=str(exc))
        return "SERIAL_ERROR", "serial error: %s" % exc
    finally:
        console.close()

    status, detail = summarize_m1(tests)
    stage(report, "m1", status, detail=detail)
    if status == "PASS":
        return "PASS", None
    if status == "INCOMPLETE":
        return "INCOMPLETE", "tests not available or not repeated: %s" % \
            ", ".join(detail)
    return "TEST_FAIL", "failed: %s" % ", ".join(detail)


# --------------------------------------------------------------------------
# P1: warm reset and long-run stability
# --------------------------------------------------------------------------

def parse_free(text):
    """Extract total/used heap from an RT-Thread `free` dump.

    RT-Thread 5.x prints "total    :" / "used     :"; older versions
    print "total memory:" / "used memory:".  Accept both.
    """
    values = {"total": None, "used": None}
    match = re.search(r"total(?:\s+memory)?\s*:\s*(\d+)", text)
    if match:
        values["total"] = int(match.group(1))
    match = re.search(r"used(?:\s+memory)?\s*:\s*(\d+)", text)
    if match:
        values["used"] = int(match.group(1))
    return values


def _reset_and_boot(console, boot_timeout):
    """Send reboot, then wait for the BSP banner and a fresh prompt."""
    started = time.monotonic()
    console.buffer = ""
    console.send("reboot")
    if not console.read_until(("reboot:",), 5):
        return {"status": "NO_REBOOT_ACK", "output": console.buffer,
                "seconds": round(time.monotonic() - started, 2)}
    # Do not clear the buffer: the banner may arrive in the same read as
    # the acknowledgement.
    if not console.read_until(RESET_MARKERS, boot_timeout):
        return {"status": "BOOT_TIMEOUT", "output": console.buffer,
                "seconds": round(time.monotonic() - started, 2)}
    if not console.wait_prompt(boot_timeout):
        return {"status": "NO_PROMPT", "output": console.buffer,
                "seconds": round(time.monotonic() - started, 2)}
    return {"status": "BOOT", "seconds": round(time.monotonic() - started, 2)}


def stability_cycle(console, args, index):
    """One reboot + identity/build-id/smoke verification cycle."""
    started = time.monotonic()
    boot = _reset_and_boot(console, args.boot_timeout)
    entry = {
        "cycle": index,
        "status": boot["status"],
        "boot": boot,
        "tests": [],
        "seconds": None,
    }
    if boot["status"] != "BOOT":
        entry["seconds"] = round(time.monotonic() - started, 2)
        return entry

    build_id, tests = m1_suite(console, 1, args.cmd_timeout,
                               args.expect_build_id)
    entry["tests"] = tests
    entry["build_id"] = build_id

    # The first smoke command measures the msh response, not a background
    # boot task: FE works the same on every cycle.
    response = tests[0].get("seconds") if tests else None
    entry["msh_response_s"] = response
    if response is not None and response > args.response_limit:
        entry["status"] = "FAIL"
        entry["detail"] = ["msh response %.2fs exceeds %.2fs" %
                           (response, args.response_limit)]
    else:
        entry["status"], entry["detail"] = summarize_m1(tests)
    entry["seconds"] = round(time.monotonic() - started, 2)
    return entry


LONG_RUN_COMMANDS = (
    ("d126_tick 100", re.compile(r"d126_tick:.*\bPASS\b")),
    ("d126_sched 100", re.compile(r"d126_sched:.*\bPASS\b")),
    ("d126_ipc", re.compile(r"d126_ipc:.*\bPASS\b")),
    ("d126_memtest 64", re.compile(r"memtest:.*\bPASS\b")),
)


def long_run(console, args):
    """Repeat the smoke set until the time budget expires.

    Heap is sampled every ten iterations; the first sample is the
    baseline.  A missing `free` command is SKIP, not PASS.
    """
    started = time.monotonic()
    deadline = started + args.duration
    iterations = 0
    failures = []
    heap = []
    heap_status = "SKIP"
    first = True

    while first or time.monotonic() < deadline:
        first = False
        for command, pass_re in LONG_RUN_COMMANDS:
            result = _command_result(console, command, args.cmd_timeout)
            entry = _classify(command, result, pass_re)
            if entry["status"] != "PASS":
                failures.append({
                    "iteration": iterations + 1,
                    "command": command,
                    "status": entry["status"],
                    "output": result["output"],
                })
                break
        if failures:
            break
        iterations += 1
        if iterations % 10 == 0:
            result = _command_result(console, "free", args.cmd_timeout)
            if result["status"] != "RUN" or MISSING_CMD_RE.search(
                    result["output"]):
                heap_status = "SKIP"
            else:
                values = parse_free(result["output"])
                if values["used"] is not None:
                    heap.append({"iteration": iterations, **values})
                    heap_status = "PASS"

    drift = None
    if heap:
        drift = heap[-1]["used"] - heap[0]["used"]
        if drift > args.heap_drift:
            failures.append({"iteration": iterations, "heap_drift": drift,
                             "limit": args.heap_drift})

    return {
        "status": "FAIL" if failures else "PASS",
        "seconds": round(time.monotonic() - started, 2),
        "iterations": iterations,
        "failures": failures,
        "heap": heap,
        "heap_status": heap_status,
        "heap_drift": drift,
    }


def run_stability_mode(args, report, report_dir):
    try:
        console = open_console(args, os.path.join(report_dir, "serial.log"),
                               args.prompt_timeout)
    except Exception as exc:
        stage(report, "serial", "SERIAL_BUSY", error=str(exc))
        return "SERIAL_BUSY", "serial busy: %s" % exc

    try:
        if not console.wait_prompt(args.prompt_timeout):
            stage(report, "serial", "NO_PROMPT")
            return "NO_PROMPT", "MSH prompt not found"

        ident = board_identity(console)
        report["firmware"]["board_identity"] = ident
        stage(report, "identity",
              "PASS" if ident["strength"] in ("strong", "weak") else "UNKNOWN",
              **ident)
        if ident["strength"] not in ("strong", "weak"):
            return "BOARD_ID_UNKNOWN", "no D126-specific answer on this port"

        result = _command_result(console, "aic_build_id", args.cmd_timeout)
        build_id = (parse_build_id_output(result["output"])
                    if result["status"] == "RUN" else None)
        report["firmware"]["build_id"] = build_id
        if args.expect_build_id and build_id != args.expect_build_id:
            stage(report, "build_id", "BUILD_ID_MISMATCH",
                  expected=args.expect_build_id, actual=build_id)
            return "BUILD_ID_MISMATCH", \
                "build_id %r != expected %r" % (build_id, args.expect_build_id)
        stage(report, "build_id", "PASS" if build_id else "SKIP",
              build_id=build_id)

        cycles = max(1, min(args.cycles, MAX_STABILITY_CYCLES))
        entries = []
        for index in range(1, cycles + 1):
            entry = stability_cycle(console, args, index)
            entries.append(entry)
            print("[hil] reset cycle %d/%d: %s (%ss)" %
                  (index, cycles, entry["status"], entry["seconds"]))
            if entry["status"] != "PASS":
                break

        long_result = None
        if all(entry["status"] == "PASS" for entry in entries) and \
                args.duration > 0:
            long_result = long_run(console, args)
            print("[hil] long run: %s (%d iterations, %ss)" %
                  (long_result["status"], long_result["iterations"],
                   long_result["seconds"]))

        report["stability"] = {
            "cycles_requested": cycles,
            "cycles_run": len(entries),
            "cycles": entries,
            "long_run": long_result,
        }
    except Exception as exc:
        stage(report, "stability", "SERIAL_ERROR", error=str(exc))
        return "SERIAL_ERROR", "serial error: %s" % exc
    finally:
        console.close()

    failed = [entry for entry in entries if entry["status"] != "PASS"]
    if failed:
        entry = failed[0]
        stage(report, "stability", entry["status"], cycle=entry["cycle"],
              detail=entry.get("detail"))
        if entry["status"] == "INCOMPLETE":
            return "INCOMPLETE", "reset cycle %d: smoke commands missing" % \
                entry["cycle"]
        return "TEST_FAIL", "reset cycle %d: %s" % (entry["cycle"],
                                                    entry["status"])
    if long_result and long_result["status"] != "PASS":
        stage(report, "stability", "TEST_FAIL", long_run=long_result)
        return "TEST_FAIL", "long run failed after %d iterations" % \
            long_result["iterations"]

    stage(report, "stability", "PASS", cycles=len(entries),
          long_run=bool(long_result))
    return "PASS", None


# --------------------------------------------------------------------------
# M2: build, flash and utest
# --------------------------------------------------------------------------

def enter_upgrade(args, report, report_dir):
    """Send aicupg over the console and wait for the D12p upgrade device."""
    command = "aicupg" if args.upgrade_target == "brom" else "aicupg gotobl"
    try:
        console = common.Console(args.port, args.baud,
                                 os.path.join(report_dir, "serial.log"))
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
        console.send(command)
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
    while time.monotonic() < deadline:
        if args.flasher == "artinchip-flash":
            paths, _ = common.aicflash_scan(flasher_exe(args), 10)
            if paths:
                stage(report, "enter_upgrade", "PASS",
                      device="USB path %s" % paths[0])
                return True, "USB path %s" % paths[0]
        else:
            stages, _ = common.upgcmd_devices(args.upgcmd, 8)
            if stages:
                stage(report, "enter_upgrade", "PASS", device=stages[0])
                return True, stages[0]
        time.sleep(0.5)

    stage(report, "enter_upgrade", "DEVICE_NOT_FOUND")
    return False, "no upgrade device appeared"


def wait_boot(args, report, report_dir):
    start = time.monotonic()
    try:
        console = open_console(args, os.path.join(report_dir, "serial.log"),
                               args.boot_timeout)
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
    try:
        console = open_console(args, os.path.join(report_dir, "serial.log"),
                               10)
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


def identify_for_flash(args, report):
    """Require at least a D126-specific console answer before flashing."""
    try:
        console = open_console(args, os.path.join(report["logs"]["dir"],
                                                  "serial.log"),
                               args.prompt_timeout)
    except Exception as exc:
        stage(report, "identity", "SERIAL_BUSY", error=str(exc))
        return False, "serial busy: %s" % exc

    try:
        if not console.wait_prompt(args.prompt_timeout):
            stage(report, "identity", "NO_PROMPT")
            return False, "MSH prompt not found before flashing"
        ident = board_identity(console)
        report["firmware"]["board_identity"] = ident
        ok = ident["strength"] in ("strong", "weak")
        stage(report, "identity", "PASS" if ok else "WRONG_BOARD", **ident)
        return ok, ("board %s" % ident.get("board") if ok
                    else "no D126-specific answer on this port")
    finally:
        console.close()


def flash_image(args, report, image, report_dir, use_partition):
    flasher = flasher_exe(args)
    if args.flasher == "artinchip-flash" and not os.path.isfile(flasher):
        return "FLASH_FAIL", "flasher not found: %s" % flasher

    log_path = os.path.join(report_dir, "flash.log")
    attempts = max(1, min(args.flash_attempts, MAX_FLASH_ATTEMPTS))
    last_status = None
    last_error = None

    for attempt in range(1, attempts + 1):
        if args.flasher == "artinchip-flash":
            cmd = [flasher, "burn", image]
            if use_partition:
                cmd += ["--partition", "os"]
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
                cmd.append(OS_COMPONENT)

        rc, timeout = run_logged(cmd, log_path, args.flash_timeout)
        if timeout:
            last_status = "FLASH_INTERRUPTED"
            last_error = "flash timed out"
        elif rc != 0:
            last_status = common.classify_flash_failure(read_text(log_path))
            last_error = "%s exited %d" % (args.flasher, rc)
        else:
            last_status = None
            last_error = None
            break

        stage(report, "flash",
              "RETRY" if attempt < attempts else "FLASH_FAIL",
              attempt=attempt, error=last_error)
        if attempt < attempts:
            time.sleep(2)
            if args.flasher == "artinchip-flash":
                paths, _ = common.aicflash_scan(flasher, 5)
                if not paths:
                    enter_upgrade(args, report, report_dir)
            else:
                stages, _ = common.upgcmd_devices(args.upgcmd, 5)
                if not stages:
                    enter_upgrade(args, report, report_dir)

    if last_error:
        return last_status or "FLASH_FAIL", last_error
    stage(report, "flash", "PASS", attempts=attempt,
          flasher=args.flasher, scope=args.flash_scope, log=log_path)
    return "PASS", None


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

    suite_count = len(args.suite) if args.suite else 6
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


def plan_mode(args, report, report_dir):
    """Local-only check.  Never opens the serial port or probes USB."""
    image, manifest_sha, build_id, manifest_path, error = resolve_image(args)
    if args.image or os.path.isfile(os.path.join(
            ARTIFACTS, args.target, "manifest.json")):
        if error:
            stage(report, "image", "IMAGE_MISSING", error=error)
            return False, error
        report["image"] = {
            "path": image,
            "sha256": sha256_file(image),
            "size": os.path.getsize(image),
            "build_id": build_id,
            "manifest": manifest_path,
        }
        report["build_id"] = build_id
        if manifest_sha and report["image"]["sha256"] != manifest_sha:
            stage(report, "image", "IMAGE_MISMATCH")
            return False, "image SHA256 does not match manifest"
        info = common.parse_image_aicflash(flasher_exe(args), image)
        report["image"]["product"] = info["product"]
        report["image"]["components"] = info["components"]
        if info["product"] != PRODUCT:
            stage(report, "image", "IMAGE_MISMATCH",
                  product=info["product"])
            return False, "image product %r != %r" % (
                info["product"], PRODUCT)
        if OS_COMPONENT not in info["components"]:
            stage(report, "image", "IMAGE_MISMATCH")
            return False, "image has no %s" % OS_COMPONENT
        selected, _ = plan_os_partition(flasher_exe(args), image)
        if selected is None:
            stage(report, "os_plan", "SKIP",
                  reason="flasher has no --partition support; "
                         "full mode will derive an os-only image")
        elif selected != [OS_COMPONENT]:
            stage(report, "os_plan", "MISMATCH", components=selected)
            return False, "--partition os selects %r" % (selected,)
        else:
            stage(report, "os_plan", "PASS", components=selected)
        stage(report, "image", "PASS", sha256=report["image"]["sha256"],
              build_id=build_id)
    else:
        stage(report, "image", "SKIP", reason="no manifest/image yet")

    print("[hil] planned: M1 serial read-only, M2 os-only flash "
          "(needs --confirm-board %s --flash-scope os)" % BOARD)
    return True, None


def main():
    common._safe_stdout()
    parser = argparse.ArgumentParser(
        description="D126 (D12p) serial/utest hardware-in-the-loop runner.")
    parser.add_argument("--mode", choices=("plan", "serial", "enter-upgrade",
                                           "stability", "full"),
                        default="plan",
                        help="plan (default), serial (M1 read-only), "
                             "enter-upgrade (no flash), stability "
                             "(reset/long-run) or full (flash+utest)")
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
                        help="USB selector for artinchip-flash (path 9-1 or "
                             "bus:address 1:25); also disambiguates a scan "
                             "that sees other ArtInChip boards")
    parser.add_argument("--image", default=None,
                        help="image path (default: manifest image)")
    parser.add_argument("--target", default=TARGET,
                        help="build-check target/profile (default: %s)" % TARGET)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--build-timeout", type=int, default=1800)
    parser.add_argument("--prompt-timeout", type=int, default=20)
    parser.add_argument("--cmd-timeout", type=int, default=20,
                        help="timeout per M1 msh command")
    parser.add_argument("--repeat", type=int, default=3,
                        help="M1 repetitions per test (default: 3)")
    parser.add_argument("--expect-build-id", default=None,
                        help="M1/stability: require this running build_id")
    parser.add_argument("--cycles", type=int, default=5,
                        help="stability: warm-reset cycles (1-%d, default: 5)"
                             % MAX_STABILITY_CYCLES)
    parser.add_argument("--duration", type=int, default=0,
                        help="stability: long-run seconds after the reset "
                             "cycles (0 = off, max %d)" % MAX_STABILITY_SECONDS)
    parser.add_argument("--response-limit", type=float, default=5.0,
                        help="stability: max seconds for one msh response "
                             "(default: 5)")
    parser.add_argument("--heap-drift", type=int, default=16384,
                        help="stability: max heap growth in long-run, bytes "
                             "(default: 16384)")
    parser.add_argument("--upgrade-timeout", type=int, default=45)
    parser.add_argument("--flash-timeout", type=int, default=300)
    parser.add_argument("--boot-timeout", type=int, default=60)
    parser.add_argument("--utest-timeout", type=int, default=120)
    parser.add_argument("--flash-attempts", type=int, default=1,
                        help="flash attempts (caps at %d)" % MAX_FLASH_ATTEMPTS)
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
    parser.add_argument("--bootstrap", action="store_true",
                        help="full mode only: the board is already in USB "
                             "upgrade mode (BOOT-KEY reset), so skip the "
                             "msh identity step")
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
        ARTIFACTS, "d126-hil", run_id)
    os.makedirs(report_dir, exist_ok=True)

    report = {
        "tool": "d126_hil",
        "schema": 1,
        "started": now_iso(),
        "finished": None,
        "duration_s": None,
        "run_id": run_id,
        "mode": args.mode,
        "status": "RUNNING",
        "error": None,
        "board": BOARD,
        "port": args.port,
        "git": {
            "branch": git("rev-parse", "--abbrev-ref", "HEAD"),
            "commit": git("rev-parse", "HEAD"),
            "dirty": bool(git("status", "--porcelain")),
        },
        "firmware": {"board_identity": None, "build_id": None},
        "image": None,
        "flash_image": None,
        "manifest": None,
        "build_id": None,
        "tests": [],
        "stability": None,
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

    # --- plan -------------------------------------------------------------
    if args.mode == "plan":
        ok, error = plan_mode(args, report, report_dir)
        return finish("DRY_RUN" if ok else "IMAGE_MISMATCH",
                      error, 0 if ok else 1)

    # --- safety gates -----------------------------------------------------
    if not args.port:
        return finish("AUTHORIZATION_REQUIRED",
                      "--port is required for serial modes", 2)
    if args.flash_transport == "uart" and args.flasher != "upgcmd":
        return finish("AUTHORIZATION_REQUIRED",
                      "--flash-transport uart requires --flasher upgcmd", 2)
    if args.flash_transport == "uart":
        # d12p_d126-demo124-nor_baremetal_bootloader_defconfig leaves
        # CONFIG_AICUPG_UART_ENABLE off, so the bootloader has no UART
        # upgrade protocol.  Refuse before any hardware state change.
        return finish("AUTHORIZATION_REQUIRED",
                      "the D126 bootloader is USB-upgrade only", 2)
    if args.mode == "stability":
        # Warm reset hides nothing on flash, but an unattended reset loop
        # is still a board state change: require the same explicit board
        # confirmation as a flash run.
        if args.confirm_board != BOARD:
            return finish("AUTHORIZATION_REQUIRED",
                          "pass --confirm-board %s before resetting the "
                          "board" % BOARD, 2)
        args.duration = max(0, min(args.duration, MAX_STABILITY_SECONDS))
    if args.mode == "full":
        if args.confirm_board != BOARD:
            return finish("AUTHORIZATION_REQUIRED",
                          "pass --confirm-board %s after checking the board "
                          "model and image" % BOARD, 2)
        if args.flash_scope == "full" and not args.confirm_full_flash:
            return finish("AUTHORIZATION_REQUIRED",
                          "--flash-scope full rewrites SPL/env; pass "
                          "--confirm-full-flash to allow it", 2)
    elif args.bootstrap:
        return finish("AUTHORIZATION_REQUIRED",
                      "--bootstrap is only valid with --mode full", 2)

    # --- M1 read-only -----------------------------------------------------
    if args.mode == "serial":
        serial_lock = common.FileLock(
            lock_path("serial-%s.lock" % args.port.replace(":", "_")))
        if not serial_lock.acquire():
            return finish("RESOURCE_BUSY", "serial lock is held")
        locks.append(serial_lock)
        status, error = run_serial_mode(args, report, report_dir)
        return finish(status, error, 0 if status == "PASS" else 1)

    # --- stability (reset + long run, no flash) ---------------------------
    if args.mode == "stability":
        serial_lock = common.FileLock(
            lock_path("serial-%s.lock" % args.port.replace(":", "_")))
        if not serial_lock.acquire():
            return finish("RESOURCE_BUSY", "serial lock is held")
        locks.append(serial_lock)
        status, error = run_stability_mode(args, report, report_dir)
        return finish(status, error, 0 if status == "PASS" else 1)

    # --- enter-upgrade only ----------------------------------------------
    if args.mode == "enter-upgrade":
        serial_lock = common.FileLock(
            lock_path("serial-%s.lock" % args.port.replace(":", "_")))
        if not serial_lock.acquire():
            return finish("RESOURCE_BUSY", "serial lock is held")
        locks.append(serial_lock)
        ok, detail = identify_for_flash(args, report)
        if not ok:
            return finish("BOARD_ID_UNKNOWN", detail)
        ok, detail = enter_upgrade(args, report, report_dir)
        return finish("UPGRADE_ENTRY_OK" if ok else "UPGRADE_ENTRY_FAIL",
                      None if ok else detail, 0 if ok else 1)

    # --- full: build ------------------------------------------------------
    build_lock = common.FileLock(lock_path("build-%s.lock" % args.target))
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
                return finish("PACKAGE_FAIL",
                              "static check passed but packaging failed")
            return finish("BUILD_FAIL", "build/check failed")
        if source_state() != before:
            return finish("PROVENANCE_INDETERMINATE",
                          "source tree changed during the build")
        stage(report, "build", "PASS",
              log=os.path.join(report_dir, "build.log"))

    image, manifest_sha, build_id, manifest_path, error = resolve_image(args)
    if error:
        stage(report, "image", "IMAGE_MISSING")
        return finish("IMAGE_MISSING", error)
    image_sha = sha256_file(image)
    if manifest_sha and image_sha != manifest_sha:
        stage(report, "image", "IMAGE_MISMATCH")
        return finish("IMAGE_MISMATCH",
                      "image SHA256 does not match manifest")
    info = common.parse_image_aicflash(flasher_exe(args), image)
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
    if OS_COMPONENT not in info["components"]:
        return finish("IMAGE_MISMATCH",
                      "image has no %s" % OS_COMPONENT)
    if not build_id:
        return finish("IMAGE_MISMATCH",
                      "manifest has no build_id; rebuild with gen_build_id")
    stage(report, "image", "PASS", sha256=image_sha, build_id=build_id)

    # Enforce os-only scope.  Prefer the flasher's own --partition
    # selection, verified locally with --plan.  If the installed tool has
    # no such option, derive an image whose only target is os; never let
    # an "os" run fall back to the full image, which would rewrite SPL/env.
    use_partition = False
    if args.flash_scope == "os" and args.flasher == "artinchip-flash":
        selected, plan_text = common.plan_os_partition(flasher_exe(args), image)
        with open(os.path.join(report_dir, "os-plan.log"), "w",
                  encoding="utf-8") as f:
            f.write(plan_text or "")
        if selected == [OS_COMPONENT]:
            use_partition = True
            report["flash_image"] = {
                "path": image,
                "sha256": image_sha,
                "components": selected,
                "mode": "partition",
            }
            stage(report, "os_image", "PASS", mode="partition",
                  components=selected)
        else:
            if selected is not None:
                stage(report, "os_image", "IMAGE_MISMATCH",
                      components=selected)
                return finish("IMAGE_MISMATCH",
                              "--partition os selects %r, not only os" %
                              (selected,))
            try:
                os_image = common.make_os_only_image(
                    image, report_dir, args.sdk or os.getenv("AIC_SDK_DIR"))
            except Exception as exc:
                stage(report, "os_image", "FAIL", error=str(exc))
                return finish("IMAGE_MISMATCH",
                              "cannot derive os-only image: %s" % exc)
            os_info = common.parse_image_aicflash(flasher_exe(args), os_image)
            if os_info["components"] != [OS_COMPONENT]:
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
                "mode": "derived",
            }
            stage(report, "os_image", "PASS", mode="derived", path=image,
                  sha256=report["flash_image"]["sha256"])

    # --- device discovery -------------------------------------------------
    scan_paths = []
    stages = []
    if args.flasher == "artinchip-flash":
        scan_paths, text = common.aicflash_scan(flasher_exe(args), 20)
    else:
        stages, text = common.upgcmd_devices(args.upgcmd)
    with open(os.path.join(report_dir, "upgcmd-list-initial.log"), "w",
              encoding="utf-8") as f:
        f.write(text)
    devices = scan_paths or stages
    stage(report, "device", "PASS" if devices else "NOT_FOUND",
          usb_paths=scan_paths, boot_stages=stages)

    # --- locks ------------------------------------------------------------
    serial_lock = common.FileLock(
        lock_path("serial-%s.lock" % args.port.replace(":", "_")))
    if not serial_lock.acquire():
        return finish("RESOURCE_BUSY", "serial lock is held")
    locks.append(serial_lock)
    target = args.device or args.port
    flash_lock = common.FileLock(
        lock_path("flash-%s.lock" % target.replace(":", "_")))
    if not flash_lock.acquire():
        return finish("RESOURCE_BUSY", "USB target lock is held")
    locks.append(flash_lock)

    if args.bootstrap:
        # First flash / recovery: the board is already in USB upgrade mode
        # (BOOT-KEY held during reset/power-on), so there is no msh to
        # identify.  Require one unambiguous target and a fully verified
        # image.  An explicit --device selector disambiguates a scan that
        # also sees other ArtInChip boards.
        if args.device:
            if not selector_known(args.device, scan_paths, text):
                return finish("DEVICE_NOT_FOUND",
                              "--device %s is not one of the scanned "
                              "devices %r" % (args.device, scan_paths), 2)
        elif len(devices) != 1:
            return finish(
                "DEVICE_AMBIGUOUS" if devices else "DEVICE_NOT_FOUND",
                "--bootstrap needs exactly one device already in upgrade "
                "mode; found %d (pass --device to select one)" %
                len(devices), 2)
        stage(report, "identity", "SKIP",
              reason="bootstrap: board already in USB upgrade mode")
    else:
        # Refuse to flash an unidentified console; this is the last point
        # before the board state changes.
        ok, detail = identify_for_flash(args, report)
        if not ok:
            return finish("BOARD_ID_UNKNOWN", detail)

    # --- enter upgrade + flash -------------------------------------------
    if args.bootstrap:
        stage(report, "enter_upgrade", "SKIP",
              reason="bootstrap: already in upgrade mode")
    else:
        # The identity step just talked to msh, so this board is running
        # the application even if the scan sees another ArtInChip board
        # (or a stale interface) already in upgrade mode.  Always ask
        # this board to enter upgrade itself.
        ok, detail = enter_upgrade(args, report, report_dir)
        if not ok:
            return finish("UPGRADE_ENTRY_FAIL", detail)

    status, error = flash_image(args, report, image, report_dir, use_partition)
    if status != "PASS":
        return finish(status, error)

    # --- boot + build id + utest -----------------------------------------
    if args.flash_transport == "usb" and args.flasher != "artinchip-flash":
        stages, _ = common.upgcmd_devices(args.upgcmd)
        if stages and args.upgrade_target == "brom":
            common.capture([args.upgcmd, "continue"], 30)

    ok, detail = wait_boot(args, report, report_dir)
    if not ok:
        return finish("BOOT_FAIL", detail)

    # Reject a board that still runs the previous image.
    ok, detail = verify_build_id(args, report, report_dir,
                                 report.get("build_id"))
    if not ok:
        return finish("BUILD_ID_MISMATCH", detail)

    result = run_utest(args, report, report_dir)
    if result != "PASS":
        return finish(result, "utest finished with %s" % result,
                      0 if result == "TEST_KNOWN_FAIL" else 1)

    return finish("PASS", None, 0)


if __name__ == "__main__":
    sys.exit(main())
