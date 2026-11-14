#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# SPDX-License-Identifier: Apache-2.0
#
# Host-side serial runner for the ArtInChip D213 utest firmware.
#
# The board must already be flashed with the d213-utest image.  The script
# sends utest_list and utest_run commands over the UART0 console, records the
# complete log and detects timeout/reset.  FPU failures are regressions now
# that the common port preserves the floating-point context.
#
# This script is read-only with respect to the target: it never flashes,
# erases or resets the board.  Reset the board manually before starting it.
#
# The serial engine is shared with d126_utest.py in utest_common.py.

import argparse
import sys

import utest_common

DEFAULT_SUITES = [
    "components.utilities.utest.uassert",
    "core.irq",
    "core.semaphore",
    "core.mutex",
    "core.mutex_pi",
    "bsp.d213.*",
]

DEFAULT_KNOWN_FAIL = []

RESET_MARKERS = (
    "ArtInChip D213 demo128 RT-Thread BSP",
    "RT-Thread 5.4.0",
)


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
    parser.add_argument("--exclude", action="append", default=None,
                        help="testcase or wildcard to skip, e.g. the FPU "
                             "suite in a non-FPU regression; may be repeated")
    parser.add_argument("--no-list", action="store_true",
                        help="skip the initial utest_list command")
    args = parser.parse_args()

    suites = args.suite or DEFAULT_SUITES
    known_fail = set(args.known_fail or []) | set(DEFAULT_KNOWN_FAIL)
    return utest_common.run_cli(args, suites, known_fail, "d213-utest",
                                "D213", RESET_MARKERS)


if __name__ == "__main__":
    sys.exit(main())
