#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# SPDX-License-Identifier: Apache-2.0
#
# Host-side serial runner for the ArtInChip D126 (D12p) utest firmware.
#
# The board must already run the d126-utest image.  This script only sends
# utest_list / utest_run over UART0 and records the result; it never flashes,
# erases or resets the target.  FPU suites are deliberately not part of the
# default list: the common port's thread switch does not preserve f0-f31 yet,
# and this phase does not accept or reject FPU results.
#
# The serial engine is shared with d213_utest.py in utest_common.py.

import argparse
import sys

import utest_common

DEFAULT_SUITES = [
    "components.utilities.utest.uassert",
    "core.irq",
    "core.semaphore",
    "core.mutex",
    "core.mutex_pi",
    "bsp.d126.*",
]

# No known failures in this phase.  Do not add the FPU context test here;
# it is retained for a future dedicated investigation and is excluded from
# the D126 utest configuration.
DEFAULT_KNOWN_FAIL = []

RESET_MARKERS = (
    "ArtInChip D126 (D12p) RT-Thread BSP",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="serial port, e.g. COM10")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--log", default=None,
                        help="log path (default: d126-utest-<timestamp>.log)")
    parser.add_argument("--timeout", type=int, default=120,
                        help="timeout per suite in seconds (default: 120)")
    parser.add_argument("--suite", action="append", default=None,
                        help="utest suite or wildcard; may be repeated")
    parser.add_argument("--known-fail", action="append", default=None,
                        help="expected failing testcase; may be repeated")
    parser.add_argument("--exclude", action="append", default=None,
                        help="testcase or wildcard to skip; may be repeated")
    parser.add_argument("--no-list", action="store_true",
                        help="skip the initial utest_list command")
    args = parser.parse_args()

    suites = args.suite or DEFAULT_SUITES
    known_fail = set(args.known_fail or []) | set(DEFAULT_KNOWN_FAIL)
    return utest_common.run_cli(args, suites, known_fail, "d126-utest",
                                "D126", RESET_MARKERS)


if __name__ == "__main__":
    sys.exit(main())
