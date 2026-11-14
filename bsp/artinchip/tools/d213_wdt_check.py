#!/usr/bin/env python3
"""D213 watchdog feed/stop/timeleft regression.

Runs over the UART MSH console on a d213 image with BSP_USING_WDT (for
example d213-usbdev-utest).  The D21x watchdog only accepts a counter
clear while the counter is between clr_thd and rst_thd, so a feed issued
too early is silently ignored by the hardware.  The steps below keep
their feeds inside that window and check the value reported by
RT_DEVICE_CTRL_WDT_GET_TIMELEFT:

  1. baseline timeleft (default timeout after the first open);
  2. start 6 s, count down, feed at ~4.5 s, time left must jump back;
  3. the board must survive past the original 6 s deadline, then stop
     must keep it alive past another 6 s;
  4. start 4 s and feed at ~2.3 s: with clr_thd clamped to rst_thd - 2
     the feed clears the counter and the board survives past 4 s;
  5. optionally start 3 s without feeding and require the SPL to report
     "Reset action: Watchdog-Reset", then the shell to come back.

Step 5 resets the board on purpose; it writes nothing to flash.

Examples:

  python d213_wdt_check.py --port COM9 --plan
  python d213_wdt_check.py --port COM9 --confirm-board d213-demo128
  python d213_wdt_check.py --port COM9 --confirm-board d213-demo128 \
      --skip-reset
"""

import argparse
import datetime
import os
import re
import sys
import time

try:
    import serial  # noqa: F401
except ImportError:
    serial = None

from d213_hil import ARTIFACTS, Console, PROMPT_RE, now_iso, write_report

BOARD = "d213-demo128"
TIMELEFT_RE = re.compile(r"watchdog time left:\s*(\d+)\s*s")
RESET_MARKER = "Watchdog-Reset"


class Failure(Exception):
    pass


def local_stamp():
    return datetime.datetime.now().strftime("%Y%m%d-%H%M%S")


def require(condition, message):
    if not condition:
        raise Failure(message)


def run_command(console, command, timeout=3.0):
    console.buffer = ""
    console.send(command)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        console.read_once()
        if PROMPT_RE.search(console.buffer):
            break
    return console.buffer


def timeleft(console, timeout=3.0):
    text = run_command(console, "wdt_test timeleft", timeout)
    match = TIMELEFT_RE.search(text)
    return int(match.group(1)) if match else None


def read_until(console, predicate, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        console.read_once()
        if predicate(console.buffer):
            return True
    return False


def alive(console, timeout=6.0):
    text = run_command(console, "aic_build_id", timeout)
    return "build_id:" in text


def step(report, name, **fields):
    entry = {"step": name, **fields}
    report["steps"].append(entry)
    return entry


def run_plan(args):
    print("D213 watchdog feed/stop/timeleft plan")
    print("  UART console : %s" % args.port)
    print("  cycles       : start/feed/stop/timeleft")
    print("  reset test   : %s" % ("skipped" if args.skip_reset
                                   else "start 3 s without feeding"))
    print("  steps:")
    print("    1. baseline wdt_test timeleft")
    print("    2. start 6, feed at ~4.5 s, timeleft must jump back")
    print("    3. survive original 6 s deadline, stop, survive past the "
          "timeout again")
    print("    4. start 4, feed at ~2.3 s, survive past 4 s (clr_thd clamp)")
    if not args.skip_reset:
        print("    5. start 3 without feed -> Watchdog-Reset boot")
    print("  warning: step 5 resets the board on purpose; it writes no flash.")
    return 0


def main():
    parser = argparse.ArgumentParser(
        description="D213 watchdog feed/stop/timeleft regression.")
    parser.add_argument("--port", default=os.getenv("D213_PORT", "COM9"),
                        help="UART console port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--confirm-board", required=True,
                        help="must be %s" % BOARD)
    parser.add_argument("--skip-reset", action="store_true",
                        help="do not run the watchdog reset injection")
    parser.add_argument("--plan", action="store_true",
                        help="print the plan and exit without opening a port")
    parser.add_argument("--report-dir",
                        default=os.path.join(ARTIFACTS, "d213-wdt"))
    args = parser.parse_args()

    if args.confirm_board != BOARD:
        parser.error("--confirm-board must be %s" % BOARD)
    if serial is None:
        parser.error("pyserial is required: pip install pyserial")
    if args.plan:
        return run_plan(args)

    report_dir = os.path.join(args.report_dir, local_stamp())
    os.makedirs(report_dir, exist_ok=True)
    report = {
        "tool": "d213_wdt_check",
        "schema": 1,
        "started": now_iso(),
        "board": BOARD,
        "uart_port": args.port,
        "steps": [],
        "status": "FAIL",
    }

    console = None
    try:
        console = Console(args.port, args.baud,
                          os.path.join(report_dir, "serial-uart.log"))
        console.open()
        require(console.wait_prompt(15), "no MSH prompt on %s" % args.port)

        left = timeleft(console)
        require(left is not None,
                "wdt_test timeleft did not answer; is BSP_USING_WDT and "
                "the timeleft action in this image?")
        step(report, "baseline", time_left=left)

        text = run_command(console, "wdt_test start 6")
        require("watchdog started" in text, "wdt_test start 6 failed: %r" % text)
        left = timeleft(console)
        require(left is not None and 5 <= left <= 6,
                "timeleft after start 6 was %r" % left)
        step(report, "start", time_left=left)

        time.sleep(4.5)
        left_before_feed = timeleft(console)
        text = run_command(console, "wdt_test feed")
        require("watchdog fed" in text, "wdt_test feed failed: %r" % text)
        left = timeleft(console)
        require(left is not None and left >= 5,
                "in-window feed did not clear the counter: before=%r after=%r"
                % (left_before_feed, left))
        step(report, "feed_in_window", before=left_before_feed, after=left)

        # The original 6 s deadline passed; only the feed keeps us alive.
        time.sleep(3.0)
        require(alive(console), "board did not survive past the original "
                                "deadline after an in-window feed")
        step(report, "feed_extends")

        text = run_command(console, "wdt_test stop")
        require("watchdog stopped" in text, "wdt_test stop failed: %r" % text)
        time.sleep(7.0)
        require(alive(console), "board reset after stop; stop did not work")
        step(report, "stop")

        # clr_thd must be clamped to rst_thd - 2: with a 4 s timeout a
        # feed at ~2.3 s is inside the window only after that clamp.
        text = run_command(console, "wdt_test start 4")
        require("watchdog started" in text, "wdt_test start 4 failed: %r" % text)
        time.sleep(2.2)
        left_before_feed = timeleft(console)
        text = run_command(console, "wdt_test feed")
        require("watchdog fed" in text, "wdt_test feed failed: %r" % text)
        left = timeleft(console)
        require(left is not None and left >= 3,
                "feed at ~2.3 s into a 4 s timeout was ignored (clr_thd "
                "clamp?): before=%r after=%r" % (left_before_feed, left))
        time.sleep(2.2)
        require(alive(console), "board reset past 4 s despite the early "
                                "in-window feed (clr_thd clamp?)")
        step(report, "clr_thd_clamp", before=left_before_feed, after=left)
        run_command(console, "wdt_test stop")

        if args.skip_reset:
            step(report, "reset_injection", status="SKIP")
        else:
            console.buffer = ""
            console.send("wdt_test start 3")
            # The SPL prints the reset reason before the app banner, so a
            # prompt after that marker can only come from the rebooted
            # shell (the command's own prompt is before the reset).
            booted = read_until(
                console,
                lambda b: RESET_MARKER in b and
                PROMPT_RE.search(b[b.find(RESET_MARKER):]),
                15.0)
            require(booted, "no boot prompt after starting a 3 s watchdog; "
                            "captured: %r" % console.buffer[-400:])
            require(RESET_MARKER in console.buffer,
                    "boot did not report %r; captured: %r"
                    % (RESET_MARKER, console.buffer[-400:]))
            require(alive(console), "shell did not answer after the "
                                    "watchdog reset")
            step(report, "reset_injection", status="PASS")

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
        print("\n[d213_wdt_check] status: %s" % report["status"])
        if report.get("error"):
            print("[d213_wdt_check] error: %s" % report["error"])
        print("[d213_wdt_check] report: %s" % report_dir)

    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
