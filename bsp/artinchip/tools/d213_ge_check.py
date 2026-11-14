#!/usr/bin/env python3
"""D213 GE Normal regression over the UART MSH console.

Runs the BSP ``ge_test`` command repeatedly on an image with the
``drivers/drv_ge.c`` adapter and the vendored GE Normal HAL (for example
d213-usbdev-utest).  Every cycle must complete all six GE operations
without the vendor HAL taking its error path:

  * fillrect and bitblt are pixel-checked by the firmware;
  * the 16x16 alpha blend is pixel-checked by the firmware;
  * scale 2x checks the four quadrants of the scaled image;
  * rotate 90 checks the red/blue halves on the long axis;
  * neither "ge timeout" nor "ge error status" may appear (the common
    RISC-V PLIC delivers a level-triggered source a second time with an
    empty status register; the GE HAL patch ignores that entry).

A cycle that prints the 256-register dump takes about 1.8 s of UART time
instead of ~70 ms, so the per-cycle wall time is recorded as a coarse
duplicate-interrupt indicator.

Examples:

  python d213_ge_check.py --port COM9 --plan
  python d213_ge_check.py --port COM9 --confirm-board d213-demo128
  python d213_ge_check.py --port COM9 --confirm-board d213-demo128 \
      --cycles 100
"""

import argparse
import datetime
import os
import sys
import time

try:
    import serial  # noqa: F401
except ImportError:
    serial = None

from d213_hil import ARTIFACTS, Console, PROMPT_RE, now_iso, write_report

BOARD = "d213-demo128"
REQUIRED = (
    ("fillrect", "ge: fillrect PASS"),
    ("bitblt", "ge: bitblt PASS"),
    ("scale", "ge: scale 2x PASS"),
    ("blend", "ge: blend PASS"),
    ("rotate", "ge: rotate 90 PASS"),
)
FORBIDDEN = (
    "ge timeout",
    "ge error status",
    "FAILED",
    "FAIL (",
)


class Failure(Exception):
    pass


def local_stamp():
    return datetime.datetime.now().strftime("%Y%m%d-%H%M%S")


def require(condition, message):
    if not condition:
        raise Failure(message)


def run_command(console, command, timeout=10.0):
    console.buffer = ""
    console.send(command)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        console.read_once()
        if PROMPT_RE.search(console.buffer):
            break
    return console.buffer


def run_plan(args):
    print("D213 GE Normal regression plan")
    print("  UART console : %s" % args.port)
    print("  command      : ge_test")
    print("  cycles       : %d" % args.cycles)
    print("  required     : fillrect PASS, bitblt PASS, scale 2x PASS,")
    print("                 blend PASS, rotate 90 PASS")
    print("  forbidden    : ge timeout, ge error status, FAILED/FAIL")
    print("  note: a timeout cycle prints a 256-register dump and takes "
          "~1.8 s instead of ~70 ms.")
    return 0


def main():
    parser = argparse.ArgumentParser(
        description="D213 GE Normal regression over the UART console.")
    parser.add_argument("--port", default=os.getenv("D213_PORT", "COM9"),
                        help="UART console port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--confirm-board", required=True,
                        help="must be %s" % BOARD)
    parser.add_argument("--cycles", type=int, default=20,
                        help="ge_test cycles to run (default 20)")
    parser.add_argument("--plan", action="store_true",
                        help="print the plan and exit without opening a port")
    parser.add_argument("--report-dir",
                        default=os.path.join(ARTIFACTS, "d213-ge"))
    args = parser.parse_args()

    if args.confirm_board != BOARD:
        parser.error("--confirm-board must be %s" % BOARD)
    if serial is None:
        parser.error("pyserial is required: pip install pyserial")
    if args.cycles < 1:
        parser.error("--cycles must be positive")
    if args.plan:
        return run_plan(args)

    report_dir = os.path.join(args.report_dir, local_stamp())
    os.makedirs(report_dir, exist_ok=True)
    report = {
        "tool": "d213_ge_check",
        "schema": 1,
        "started": now_iso(),
        "board": BOARD,
        "uart_port": args.port,
        "command": "ge_test",
        "cycles": args.cycles,
        "steps": [],
        "status": "FAIL",
    }

    console = None
    try:
        console = Console(args.port, args.baud,
                          os.path.join(report_dir, "serial-uart.log"))
        console.open()
        require(console.wait_prompt(15), "no MSH prompt on %s" % args.port)

        text = run_command(console, "aic_build_id")
        require("build_id:" in text,
                "aic_build_id did not answer; wrong image?")

        for cycle in range(1, args.cycles + 1):
            started = time.monotonic()
            text = run_command(console, "ge_test")
            elapsed_ms = int((time.monotonic() - started) * 1000)

            require("ge: version" in text,
                    "cycle %d: ge_test did not run; captured: %r"
                    % (cycle, text[-300:]))
            for line in FORBIDDEN:
                if line in text:
                    raise Failure(
                        "cycle %d: %r in ge_test output; captured: %r"
                        % (cycle, line, text[-500:]))
            for name, expected in REQUIRED:
                require(expected in text,
                        "cycle %d: missing %r; captured: %r"
                        % (cycle, expected, text[-500:]))

            report["steps"].append({
                "cycle": cycle,
                "elapsed_ms": elapsed_ms,
                "status": "PASS",
            })

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
        print("\n[d213_ge_check] status: %s" % report["status"])
        if report.get("error"):
            print("[d213_ge_check] error: %s" % report["error"])
        print("[d213_ge_check] report: %s" % report_dir)

    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
