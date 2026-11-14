#!/usr/bin/env python3
"""D213 storage recovery soak test.

This script drives the already-flashed d213-utest firmware over the serial
console.  It never flashes, formats or writes a declared partition.  The
only destructive operations are the MSH commands that were added for the
storage phase-2 acceptance:

  nand_lfs_bbt_test confirm   erase the active BBT slot in nand_scratch
  wdt_test start <seconds>    reset the board through the watchdog
  nand_lfs_stress start <s>   write /nand/stress.bin until the reset

The BBT test is run once.  The stress test can be repeated because it does
not erase the BBT.  A failed BBT recovery or a filesystem that no longer
mounts is reported as FAIL, not silently skipped.

Examples:

  python d213_storage_soak.py --port COM6 --plan
  python d213_storage_soak.py --port COM6 --confirm-board d213-demo128 \
      --confirm-storage --stress-cycles 2
"""

import argparse
import datetime
import json
import os
import re
import sys
import time

try:
    import serial  # noqa: F401
except ImportError:
    serial = None

from d213_hil import Console, PROMPT_RE, now_iso, write_report

BOARD = "d213-demo128"
DEFAULT_STRESS_SECONDS = 30
DEFAULT_RESET_TIMEOUT = 15


class Failure(Exception):
    pass


def local_stamp():
    return datetime.datetime.now().strftime("%Y%m%d-%H%M%S")


def run_command(console, command, timeout=10.0):
    """Send one MSH command and return its output up to the next prompt."""
    console.buffer = ""
    console.send(command)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        console.read_once()
        if PROMPT_RE.search(console.buffer):
            break
    return console.buffer


def wait_for_prompt_after_reset(console, timeout):
    """Clear the console buffer and wait for the post-reset prompt."""
    console.buffer = ""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        console.read_once()
        if PROMPT_RE.search(console.buffer):
            return console.buffer
    return None


def parse_lfs_info(text):
    info = {}
    match = re.search(
        r"BBT:\s*seq\s*(\d+),\s*slots\s*(\d+)/(\d+),\s*active\s*(-?\d+)",
        text)
    if match:
        info["sequence"] = int(match.group(1))
        info["slots"] = [int(match.group(2)), int(match.group(3))]
        info["active"] = int(match.group(4))
    match = re.search(
        r"littlefs:\s*mounted at /nand,\s*used\s*(\d+)/(\d+)\s*blocks",
        text)
    if match:
        info["used_blocks"] = int(match.group(1))
        info["logical_blocks"] = int(match.group(2))
        info["mounted"] = True
    else:
        info["mounted"] = False
    return info


def parse_bbt_test(text):
    match = re.search(
        r"BBT slot (\d+) \(block (\d+), seq (\d+)\) erased;\s*"
        r"reboot to recover from slot (\d+)", text)
    if not match:
        return None
    return {
        "slot": int(match.group(1)),
        "block": int(match.group(2)),
        "sequence": int(match.group(3)),
        "other_slot": int(match.group(4)),
    }


def parse_stress_check(text):
    if "is missing" in text:
        return {"present": False}
    match = re.search(
        r"magic ok, seq (\d+), data (\d+) bytes, payload (ok|MISMATCH)",
        text)
    if not match:
        return None
    return {
        "present": True,
        "sequence": int(match.group(1)),
        "data_size": int(match.group(2)),
        "payload": match.group(3),
    }


def parse_ota_status(text):
    status = {}
    match = re.search(r"active\s*:\s*(env_r|env|none)", text)
    if match:
        status["active"] = match.group(1)
    for key in ("osAB_now", "osAB_next"):
        match = re.search(r"%s\s*:\s*([A-Za-z0-9_]+)" % key, text)
        if match:
            status[key] = match.group(1)
    for slot in ("os", "os_r"):
        match = re.search(r"FIT\s+%s\s*:\s*(valid|no valid FIT header)" % slot,
                          text)
        if match:
            status["fit_" + slot] = match.group(1)
    return status


def parse_partitions(text):
    parts = {}
    for slot in ("os", "os_r"):
        if re.search(r"^\s*%s\s*:\s*FIT image detected" % slot, text,
                     re.MULTILINE):
            parts[slot] = "valid"
        elif re.search(r"^\s*%s\s*:\s*no valid FIT header" % slot, text,
                       re.MULTILINE):
            parts[slot] = "empty"
        elif re.search(r"^\s*%s\s*:\s*header read failed" % slot, text,
                       re.MULTILINE):
            parts[slot] = "read-error"
        else:
            parts[slot] = "unknown"
    return parts


def require(condition, message):
    if not condition:
        raise Failure(message)


def record_step(report, name, status, **extra):
    entry = {"status": status}
    entry.update(extra)
    report["steps"].append({"name": name, **entry})
    return entry


def run_plan(args):
    print("D213 storage soak plan")
    print("  port            : %s" % args.port)
    print("  board           : %s" % args.confirm_board)
    print("  BBT test        : %s" % ("yes" if not args.skip_bbt else "no"))
    print("  stress cycles   : %d" % args.stress_cycles)
    print("  stress duration : %d s (watchdog resets earlier)" %
          args.stress_seconds)
    print("  destructive scope: nand_scratch BBT slot and /nand/stress.bin")
    print("  declared partitions: never written")
    print("  steps:")
    print("    1. nand_lfs_info / aic_ota_slot_status / aic_nand_partitions")
    print("    2. nand_lfs_bbt_test confirm + wdt reset + verify seq-1")
    print("    3. wdt_test start + nand_lfs_stress start + reset + check")
    print("    4. final env/FIT/BBT verification")
    return 0


def main():
    parser = argparse.ArgumentParser(
        description="D213 storage BBT switch and reset-injection soak test.")
    parser.add_argument("--port", default=os.getenv("D213_PORT", "COM6"))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--confirm-board", required=True,
                        help="must be %s" % BOARD)
    parser.add_argument("--confirm-storage", action="store_true",
                        help="required for the destructive BBT/stress steps")
    parser.add_argument("--stress-cycles", type=int, default=2)
    parser.add_argument("--stress-seconds", type=int,
                        default=DEFAULT_STRESS_SECONDS)
    parser.add_argument("--reset-timeout", type=int,
                        default=DEFAULT_RESET_TIMEOUT)
    parser.add_argument("--skip-bbt", action="store_true",
                        help="skip the one BBT slot erase")
    parser.add_argument("--plan", action="store_true",
                        help="print the plan and exit without opening the port")
    parser.add_argument("--report-dir",
                        default=os.path.join("artifacts", "d213-storage-soak"))
    args = parser.parse_args()

    if args.confirm_board != BOARD:
        parser.error("--confirm-board must be %s" % BOARD)
    if args.stress_cycles < 0:
        parser.error("--stress-cycles must be >= 0")
    if args.stress_seconds < 1:
        parser.error("--stress-seconds must be >= 1")
    if args.stress_seconds <= args.reset_timeout + 2:
        parser.error("--stress-seconds must exceed --reset-timeout by >2 s "
                     "so a completed stress run cannot be mistaken for a reset")
    if args.plan:
        return run_plan(args)
    if not args.confirm_storage:
        parser.error("--confirm-storage is required for the destructive test")
    if args.stress_cycles == 0 and args.skip_bbt:
        parser.error("nothing to do: both BBT and stress are disabled")

    report_dir = os.path.join(args.report_dir, local_stamp())
    os.makedirs(report_dir, exist_ok=True)
    log_path = os.path.join(report_dir, "serial.log")
    report = {
        "tool": "d213_storage_soak",
        "schema": 1,
        "started": now_iso(),
        "board": BOARD,
        "port": args.port,
        "steps": [],
        "status": "FAIL",
    }

    console = None
    try:
        console = Console(args.port, args.baud, log_path)
        console.open()
        require(console.wait_prompt(15), "no MSH prompt on %s" % args.port)

        # ---- baseline -------------------------------------------------
        lfs_text = run_command(console, "nand_lfs_info", 10)
        lfs = parse_lfs_info(lfs_text)
        require("sequence" in lfs, "cannot parse nand_lfs_info BBT line")
        require(lfs["mounted"], "/nand is not mounted")
        require(lfs["active"] in (0, 1), "BBT has no active slot")
        record_step(report, "baseline_lfs", "PASS", **lfs)

        ota_text = run_command(console, "aic_ota_slot_status", 10)
        ota = parse_ota_status(ota_text)
        require(ota.get("active") in ("env", "env_r"),
                "no valid Bootloader env copy")
        require(ota.get("osAB_now") in ("A", "B"), "osAB_now is missing")
        require(ota.get("fit_os") == "valid", "os FIT header is not valid")
        record_step(report, "baseline_ota", "PASS", **ota)

        part_text = run_command(console, "aic_nand_partitions", 20)
        parts = parse_partitions(part_text)
        require(parts.get("os") == "valid", "aic_nand_partitions: os FIT missing")
        record_step(report, "baseline_partitions", "PASS", **parts)

        # ---- one BBT slot switch --------------------------------------
        if not args.skip_bbt:
            old_seq = lfs["sequence"]
            old_active = lfs["active"]
            require(old_seq >= 1,
                    "BBT sequence is 0; the other slot may be empty, "
                    "refusing the destructive switch")

            bbt_text = run_command(console, "nand_lfs_bbt_test confirm", 10)
            erased = parse_bbt_test(bbt_text)
            require(erased is not None, "nand_lfs_bbt_test did not confirm")
            require(erased["slot"] == old_active,
                    "nand_lfs_bbt_test erased slot %d, expected %d" %
                    (erased["slot"], old_active))

            run_command(console, "wdt_test start 2", 3)
            require(wait_for_prompt_after_reset(console, args.reset_timeout),
                    "board did not return to the prompt after the BBT reset")

            after_text = run_command(console, "nand_lfs_info", 10)
            after = parse_lfs_info(after_text)
            require("sequence" in after, "cannot parse BBT after reset")
            require(after["mounted"], "/nand did not mount after BBT recovery")
            require(after["active"] == (old_active ^ 1),
                    "BBT recovered slot %d, expected %d" %
                    (after["active"], old_active ^ 1))
            record_step(report, "bbt_switch", "PASS",
                        before_sequence=old_seq,
                        after_sequence=after["sequence"],
                        before_active=old_active,
                        after_active=after["active"],
                        recovered=after["sequence"] == old_seq - 1)
            if after["sequence"] != old_seq - 1:
                record_step(report, "bbt_sequence", "WARN",
                            detail="expected sequence one lower")

        # ---- repeated power-loss / watchdog resets --------------------
        stress_results = []
        for index in range(args.stress_cycles):
            run_command(console, "wdt_test start 3", 3)
            console.buffer = ""
            console.send("nand_lfs_stress start %d" % args.stress_seconds)
            console.drain(0.5)
            if PROMPT_RE.search(console.buffer) and "wrote " not in console.buffer:
                raise Failure("cycle %d: stress command returned before the "
                              "watchdog reset: %s" %
                              (index + 1, console.buffer.strip()))
            require(wait_for_prompt_after_reset(console, args.reset_timeout),
                    "cycle %d did not return to the prompt after reset" %
                    (index + 1))

            info_text = run_command(console, "nand_lfs_info", 10)
            info = parse_lfs_info(info_text)
            require("sequence" in info, "cycle %d: cannot parse BBT" %
                    (index + 1))
            require(info["mounted"], "cycle %d: /nand did not mount" %
                    (index + 1))

            check_text = run_command(console, "nand_lfs_stress check", 10)
            check = parse_stress_check(check_text)
            require(check is not None,
                    "cycle %d: neither a valid header nor 'missing'" %
                    (index + 1))
            if check["present"]:
                require(check["data_size"] == 4096,
                        "cycle %d: stress header data_size=%d" %
                        (index + 1, check["data_size"]))
                require(check["payload"] == "ok",
                        "cycle %d: stress payload mismatch" % (index + 1))
            stress_results.append({
                "cycle": index + 1,
                "mount": "PASS",
                "file": "valid" if check["present"] else "missing",
                "sequence": check.get("sequence"),
            })
        record_step(report, "stress_resets", "PASS", cycles=stress_results)

        # ---- final read-only verification -----------------------------
        final_ota_text = run_command(console, "aic_ota_slot_status", 10)
        final_ota = parse_ota_status(final_ota_text)
        require(final_ota.get("active") in ("env", "env_r"),
                "env became invalid after the resets")
        require(final_ota.get("fit_os") == "valid",
                "os FIT became invalid after the resets")
        record_step(report, "final_ota", "PASS", **final_ota)

        final_part_text = run_command(console, "aic_nand_partitions", 20)
        final_parts = parse_partitions(final_part_text)
        require(final_parts.get("os") == "valid",
                "aic_nand_partitions: os FIT missing after the resets")
        record_step(report, "final_partitions", "PASS", **final_parts)

        report["status"] = "PASS"
    except Failure as exc:
        report["error"] = str(exc)
    except Exception as exc:  # noqa: BLE001
        report["error"] = "%s: %s" % (type(exc).__name__, exc)
    finally:
        if console is not None:
            console.close()
        report["finished"] = now_iso()
        write_report(report, report_dir)
        print("\n[d213_storage_soak] status: %s" % report["status"])
        if report.get("error"):
            print("[d213_storage_soak] error: %s" % report["error"])
        print("[d213_storage_soak] report: %s" % report_dir)

    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
