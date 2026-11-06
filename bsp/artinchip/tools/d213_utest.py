#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# SPDX-License-Identifier: Apache-2.0
#
# Host-side serial runner for the ArtInChip D213 utest firmware.
#
# The board must already be flashed with the d213-utest image.  The script
# sends utest_list and utest_run commands over the UART0 console, records the
# complete log, detects timeout/reset, and distinguishes expected FPU
# known-failures from real regressions.
#
# This script is read-only with respect to the target: it never flashes,
# erases or resets the board.  Reset the board manually before starting it.

import argparse
import datetime
import os
import re
import sys
import time

try:
    import serial
except ImportError:
    serial = None


DEFAULT_SUITES = [
    "components.utilities.utest.uassert",
    "core.irq",
    "core.semaphore",
    "core.mutex",
    "core.mutex_pi",
    "bsp.d213.*",
]

DEFAULT_KNOWN_FAIL = [
    # The pure BSP uses the upstream common port, which does not preserve
    # callee-saved FPU registers across thread switches.
    "bsp.d213.fpu.context",
]

PASS_RE = re.compile(r"\[\s+PASSED\s+\]\s+\[\s+result\s+\]\s+testcase\s+\((.+?)\)")
FAIL_RE = re.compile(r"\[\s+FAILED\s+\]\s+\[\s+result\s+\]\s+testcase\s+\((.+?)\)")
INIT_FAIL_RE = re.compile(
    r"\[\s+FAILED\s+\]\s+\[\s+result\s+\]\s+testcase\s+(?:init|cleanup)\s+\((.+?)\)")
LIST_RE = re.compile(r"\[testcase name\]:(.+?);")
RESET_MARKERS = (
    "ArtInChip D213 demo128 RT-Thread BSP",
    "RT-Thread 5.3.0",
)


class Runner:
    def __init__(self, port, baud, log_path):
        self.ser = serial.Serial(port, baud, timeout=0.1)
        self.log = open(log_path, "a", encoding="utf-8", errors="replace")
        self.buffer = ""
        self.started = datetime.datetime.now()
        self._log_header(port, baud, log_path)

    def _log_header(self, port, baud, log_path):
        stamp = self.started.strftime("%Y-%m-%d %H:%M:%S")
        header = (
            "# ArtInChip D213 utest serial log\n"
            "# start: %s\n"
            "# port: %s baud: %d\n"
            "# log: %s\n"
            "#\n"
            % (stamp, port, baud, log_path)
        )
        self.log.write(header)
        self.log.flush()

    def close(self):
        try:
            self.ser.close()
        finally:
            self.log.close()

    def _read_once(self):
        data = self.ser.read(4096)
        if not data:
            return ""
        text = data.decode("utf-8", "replace")
        self.buffer += text
        self.log.write(text)
        self.log.flush()
        sys.stdout.write(text)
        sys.stdout.flush()
        return text

    def _clear(self):
        self.buffer = ""

    def send(self, command):
        self.ser.write((command + "\r\n").encode("utf-8"))

    def wait_for_prompt(self, timeout):
        deadline = time.monotonic() + timeout
        nudged = False
        while time.monotonic() < deadline:
            self._read_once()
            if "msh >" in self.buffer:
                return True
            if not nudged and time.monotonic() - (deadline - timeout) > 2.0:
                self.send("")
                nudged = True
        return False

    def drain(self, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            self._read_once()

    def run_list(self, timeout):
        self._clear()
        self.send("utest_list")
        if not self.wait_for_prompt(timeout):
            return None
        return self.buffer

    def run_suite(self, suite, timeout):
        self._clear()
        self.send("utest_run -thread " + suite)
        deadline = time.monotonic() + timeout
        reset = False
        while time.monotonic() < deadline:
            self._read_once()
            if any(marker in self.buffer for marker in RESET_MARKERS):
                reset = True
                break
            if "[==========] [ utest    ] finished" in self.buffer:
                self.drain(0.3)
                break
        else:
            return {
                "suite": suite,
                "status": "RESET" if reset else "TIMEOUT",
                "passed": [],
                "failed": {},
                "seconds": timeout,
                "raw": self.buffer,
            }

        if reset:
            return {
                "suite": suite,
                "status": "RESET",
                "passed": [],
                "failed": {},
                "seconds": time.monotonic() - (deadline - timeout),
                "raw": self.buffer,
            }

        passed = PASS_RE.findall(self.buffer)
        failed = {}
        for name in FAIL_RE.findall(self.buffer) + INIT_FAIL_RE.findall(self.buffer):
            failed[name] = "testcase"

        return {
            "suite": suite,
            "status": "RUN",
            "passed": passed,
            "failed": failed,
            "seconds": time.monotonic() - (deadline - timeout),
            "raw": self.buffer,
        }


def classify(result, known_fail):
    if result["status"] != "RUN":
        return result["status"], []

    failed = set(result["failed"])
    known = sorted(failed & known_fail)
    unexpected = sorted(failed - known_fail)

    if "Not find" in result["raw"]:
        return "FAIL", ["suite not found"]
    if unexpected:
        return "FAIL", unexpected
    if not result["passed"] and not failed:
        return "FAIL", ["no testcase result parsed"]
    if known:
        return "KNOWN-FAIL", known
    return "PASS", []


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="serial port, e.g. COM7")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--log", default=None,
                        help="log path (default: d213-utest-<timestamp>.log)")
    parser.add_argument("--timeout", type=int, default=180,
                        help="timeout per suite in seconds (default: 180)")
    parser.add_argument("--suite", action="append", default=None,
                        help="utest suite or wildcard; may be repeated")
    parser.add_argument("--known-fail", action="append", default=None,
                        help="expected failing testcase; may be repeated")
    parser.add_argument("--no-list", action="store_true",
                        help="skip the initial utest_list command")
    args = parser.parse_args()

    if serial is None:
        sys.exit("pyserial is required: pip install pyserial")

    suites = args.suite or DEFAULT_SUITES
    known_fail = set(args.known_fail or []) | set(DEFAULT_KNOWN_FAIL)
    log_path = args.log or ("d213-utest-%s.log" %
                            datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))

    runner = Runner(args.port, args.baud, log_path)
    try:
        if not runner.wait_for_prompt(20):
            print("\nERROR: MSH prompt not found; is the board running?")
            return 1
        runner.drain(0.2)

        listed = []
        if not args.no_list:
            text = runner.run_list(20)
            if text is None:
                print("\nERROR: utest_list did not return to the prompt")
                return 1
            listed = [name.strip() for name in LIST_RE.findall(text)]
            print("\nDiscovered testcases: %d" % len(listed))
            for name in listed:
                print("  " + name)

            missing = []
            for suite in suites:
                if suite.endswith("*"):
                    prefix = suite[:-1]
                    if not any(name.startswith(prefix) for name in listed):
                        missing.append(suite)
                elif suite not in listed:
                    missing.append(suite)
            if missing:
                print("\nERROR: testcases missing from utest_list: " +
                      ", ".join(missing))
                return 1

        results = []
        for suite in suites:
            result = runner.run_suite(suite, args.timeout)
            status, details = classify(result, known_fail)
            result["status"] = status
            result["details"] = details
            results.append(result)
            print("\n[%s] %s (%.2fs, %d passed, %d failed)" %
                  (status, suite, result["seconds"],
                   len(result["passed"]), len(result["failed"])))
            if details:
                print("  " + ", ".join(details))

        print("\nArtInChip D213 utest summary")
        print("-----------------------------")
        for result in results:
            print("%-12s %-40s %.2fs" %
                  (result["status"], result["suite"], result["seconds"]))
        print("log: " + os.path.abspath(log_path))

        if any(r["status"] in ("TIMEOUT", "RESET", "FAIL") for r in results):
            return 1
        if any(r["status"] == "KNOWN-FAIL" for r in results):
            return 2
        return 0
    finally:
        runner.close()


if __name__ == "__main__":
    sys.exit(main())
