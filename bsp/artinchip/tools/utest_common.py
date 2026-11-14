#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# SPDX-License-Identifier: Apache-2.0
#
# Board-independent serial runner for RT-Thread utest firmware.
#
# The caller owns the suite list, the board reset markers and the known
# failures; this module only drives the console, parses uassert output and
# classifies timeouts/resets.  d213_utest.py and d126_utest.py are thin
# board-specific front ends.

import datetime
import fnmatch
import os
import re
import sys
import time

try:
    import serial
except ImportError:
    serial = None

# DFS changes the MSH prompt to "msh />" (it prints the working
# directory), so accept both forms.
PROMPT_RE = re.compile(r"msh\s+(?:[^>\s]+\s*)?>")

PASS_RE = re.compile(r"\[\s+PASSED\s+\]\s+\[\s+result\s+\]\s+testcase\s+\((.+?)\)")
FAIL_RE = re.compile(r"\[\s+FAILED\s+\]\s+\[\s+result\s+\]\s+testcase\s+\((.+?)\)")
INIT_FAIL_RE = re.compile(
    r"\[\s+FAILED\s+\]\s+\[\s+result\s+\]\s+testcase\s+(?:init|cleanup)\s+\((.+?)\)")
LIST_RE = re.compile(r"\[testcase name\]:(.+?);")
FINISHED = "[==========] [ utest    ] finished"


class Runner:
    def __init__(self, port, baud, log_path, board_label="D213",
                 reset_markers=None):
        self.ser = serial.Serial(port, baud, timeout=0.1)
        self.log = open(log_path, "a", encoding="utf-8", errors="replace")
        self.buffer = ""
        self.board_label = board_label
        self.reset_markers = tuple(reset_markers or ())
        self.started = datetime.datetime.now()
        self._log_header(port, baud, log_path)

    def _log_header(self, port, baud, log_path):
        stamp = self.started.strftime("%Y-%m-%d %H:%M:%S")
        header = (
            "# ArtInChip %s utest serial log\n"
            "# start: %s\n"
            "# port: %s baud: %d\n"
            "# log: %s\n"
            "#\n"
            % (self.board_label, stamp, port, baud, log_path)
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
            if "msh >" in self.buffer or PROMPT_RE.search(self.buffer):
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
            if any(marker in self.buffer for marker in self.reset_markers):
                reset = True
                break
            if FINISHED in self.buffer:
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


def check_listed(listed, suites):
    """Return the suites that utest_list did not advertise."""
    missing = []
    for suite in suites:
        if suite.endswith("*"):
            prefix = suite[:-1]
            if not any(name.startswith(prefix) for name in listed):
                missing.append(suite)
        elif suite not in listed:
            missing.append(suite)
    return missing


def expand_suites(listed, suites, excludes):
    """Expand wildcard suites, then drop testcases matching --exclude.

    A phase may need to run every suite except a known-out-of-scope
    group (for example the D213 FPU context test).  Exclusion happens
    only when --exclude is given; without it the suite list is passed to
    utest unchanged, so the existing command behaviour is preserved.
    """
    if not excludes:
        return suites

    expanded = []
    for suite in suites:
        if suite.endswith("*"):
            prefix = suite[:-1]
            expanded.extend(name for name in listed
                            if name.startswith(prefix))
        else:
            expanded.append(suite)

    return [name for name in expanded
            if not any(fnmatch.fnmatchcase(name, pattern)
                       for pattern in excludes)]


def run_cli(args, suites, known_fail, log_prefix, board_label,
            reset_markers):
    """Shared CLI body; returns the process exit code."""
    if serial is None:
        sys.exit("pyserial is required: pip install pyserial")

    excludes = list(getattr(args, "exclude", None) or [])
    if excludes and args.no_list:
        print("ERROR: --exclude requires utest_list; do not use --no-list")
        return 1

    log_path = args.log or ("%s-%s.log" %
                            (log_prefix,
                             datetime.datetime.now().strftime("%Y%m%d-%H%M%S")))

    runner = Runner(args.port, args.baud, log_path, board_label,
                    reset_markers)
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

            missing = check_listed(listed, suites)
            if missing:
                print("\nERROR: testcases missing from utest_list: " +
                      ", ".join(missing))
                return 1

            if excludes:
                suites = expand_suites(listed, suites, excludes)
                if not suites:
                    print("\nERROR: --exclude removed every testcase")
                    return 1
                print("\nExcluded by request: " + ", ".join(excludes))
                print("Selected testcases: %d" % len(suites))

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

        print("\nArtInChip %s utest summary" % board_label)
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
