#!/usr/bin/env python3
"""D213 USB0 CDC ACM console-switch regression.

Runs on the already-flashed d213-usbdev-utest firmware with both the UART
console (COM port) and the USB CDC ACM console (the board's USB0 device
port) attached.  Each cycle:

  1. 'usb_dev_console on' is sent over the UART console;
  2. 'usb_dev_status' must answer over the USB console with
     "console=usb-acm0", which proves USB input and output;
  3. 'usb_dev_console off' is sent over the USB console;
  4. 'usb_dev_status' must answer over the UART console with
     "console=uart0".  This query is also the input test: without the
     driver's open-ordering fix, uart0 keeps its RX FIFO but loses
     RT_DEVICE_FLAG_INT_RX, the shell polls the hardware FIFO while the
     1 ms RX poller drains it into the software ring, and the UART
     console never accepts input again until the board is reset.

WARNING: if the firmware under test lacks the fix, step 4 fails and only
a physical reset recovers the board.  Run this on a bench with the reset
button reachable.

Examples:

  python d213_usb_console.py --port COM9 --cdc-port COM12 --plan
  python d213_usb_console.py --port COM9 --confirm-board d213-demo128
  python d213_usb_console.py --port COM9 --confirm-board d213-demo128 \
      --cycles 5
"""

import argparse
import os
import sys
import time

try:
    import serial  # noqa: F401
    import serial.tools.list_ports
except ImportError:
    serial = None

from d213_hil import ARTIFACTS, Console, PROMPT_RE, now_iso, write_report

BOARD = "d213-demo128"
USB_VID = "33C3"
USB_PID = "6688"
DEFAULT_CYCLES = 3


class Failure(Exception):
    pass


def local_stamp():
    import datetime
    return datetime.datetime.now().strftime("%Y%m%d-%H%M%S")


def find_cdc_port():
    """Return the board's CDC ACM port name, or None."""
    forms = (
        "VID_%s&PID_%s" % (USB_VID, USB_PID),   # Windows
        "VID:PID=%s:%s" % (USB_VID, USB_PID),   # pyserial cross-platform
    )
    for port in serial.tools.list_ports.comports():
        hwid = (port.hwid or "").upper()
        if any(form in hwid for form in forms):
            return port.device
    return None


def require(condition, message):
    if not condition:
        raise Failure(message)


def run_command(console, command, timeout):
    console.buffer = ""
    console.send(command)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        console.read_once()
        if PROMPT_RE.search(console.buffer):
            return console.buffer
    return console.buffer


def status_on(console, expected_console, timeout=5.0):
    """Query usb_dev_status and return its text, or None if it did not
    answer with the expected console=device field in time."""
    text = run_command(console, "usb_dev_status", timeout)
    if expected_console in text and "configured=1" in text:
        return text
    return None


def status_with_retry(console, expected_console, attempts=3, timeout=2.0):
    for _ in range(attempts):
        text = status_on(console, expected_console, timeout)
        if text is not None:
            return text
        time.sleep(0.5)
    return None


def run_plan(args):
    print("D213 USB console switch plan")
    print("  UART console : %s" % args.port)
    print("  USB console  : %s" % (args.cdc_port or "auto-detect (VID %s PID %s)"
                                   % (USB_VID, USB_PID)))
    print("  cycles       : %d" % args.cycles)
    print("  firmware     : d213-usbdev-utest with usb_dev_console")
    print("  steps per cycle:")
    print("    1. usb_dev_console on  (UART)  -> prompt on USB console")
    print("    2. usb_dev_status      (USB)   -> console=usb-acm0")
    print("    3. usb_dev_console off (USB)   -> prompt on UART console")
    print("    4. usb_dev_status      (UART)  -> console=uart0 (input test)")
    print("  warning: without the open-ordering fix, step 4 fails and the")
    print("           board needs a physical reset.")
    return 0


def main():
    parser = argparse.ArgumentParser(
        description="D213 USB0 CDC ACM console-switch regression.")
    parser.add_argument("--port", default=os.getenv("D213_PORT", "COM9"),
                        help="UART console port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--cdc-port", default=None,
                        help="USB CDC ACM port; auto-detected by VID/PID "
                             "when omitted")
    parser.add_argument("--cycles", type=int, default=DEFAULT_CYCLES)
    parser.add_argument("--confirm-board", required=True,
                        help="must be %s" % BOARD)
    parser.add_argument("--plan", action="store_true",
                        help="print the plan and exit without opening a port")
    parser.add_argument("--report-dir",
                        default=os.path.join(ARTIFACTS, "d213-usb-console"))
    args = parser.parse_args()

    if args.confirm_board != BOARD:
        parser.error("--confirm-board must be %s" % BOARD)
    if args.cycles < 1:
        parser.error("--cycles must be >= 1")
    if serial is None:
        parser.error("pyserial is required: pip install pyserial")
    if args.plan:
        return run_plan(args)

    report_dir = os.path.join(args.report_dir, local_stamp())
    os.makedirs(report_dir, exist_ok=True)
    report = {
        "tool": "d213_usb_console",
        "schema": 1,
        "started": now_iso(),
        "board": BOARD,
        "uart_port": args.port,
        "cdc_port": args.cdc_port,
        "cycles": [],
        "status": "FAIL",
    }

    uart = None
    try:
        uart = Console(args.port, args.baud,
                       os.path.join(report_dir, "serial-uart.log"))
        uart.open()
        require(uart.wait_prompt(15), "no MSH prompt on %s" % args.port)

        for cycle in range(1, args.cycles + 1):
            cdc_port = args.cdc_port or find_cdc_port()
            require(cdc_port is not None,
                    "cycle %d: no CDC ACM port with VID %s PID %s"
                    % (cycle, USB_VID, USB_PID))
            cdc = Console(cdc_port, args.baud,
                          os.path.join(report_dir, "serial-cdc.log"))
            cdc.open()
            entry = {"cycle": cycle, "cdc_port": cdc_port}
            try:
                uart.buffer = ""
                uart.send("usb_dev_console on")
                require(cdc.wait_prompt(10),
                        "cycle %d: no prompt on USB console after 'on'; "
                        "UART said: %r" % (cycle, uart.buffer))
                text = status_with_retry(cdc, "console=usb-acm0")
                require(text is not None,
                        "cycle %d: USB console did not answer usb_dev_status"
                        % cycle)
                entry["usb"] = "PASS"

                cdc.buffer = ""
                cdc.send("usb_dev_console off")
                time.sleep(0.5)
                text = status_with_retry(uart, "console=uart0")
                require(text is not None,
                        "cycle %d: UART console did not answer after 'off'; "
                        "input path is broken (physical reset may be needed)"
                        % cycle)
                entry["uart"] = "PASS"
                report["cycles"].append(entry)
                print("[PASS] cycle %d: USB console up and UART console "
                      "restored" % cycle)
            except Failure as exc:
                entry["status"] = "FAIL"
                entry["error"] = str(exc)
                report["cycles"].append(entry)
                raise
            finally:
                cdc.close()

        report["status"] = "PASS"
    except Failure as exc:
        report["error"] = str(exc)
    except Exception as exc:  # noqa: BLE001
        report["error"] = "%s: %s" % (type(exc).__name__, exc)
    finally:
        if uart is not None:
            uart.close()
        report["finished"] = now_iso()
        write_report(report, report_dir)
        print("\n[d213_usb_console] status: %s" % report["status"])
        if report.get("error"):
            print("[d213_usb_console] error: %s" % report["error"])
        print("[d213_usb_console] report: %s" % report_dir)

    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
