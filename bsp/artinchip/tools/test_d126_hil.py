#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# SPDX-License-Identifier: Apache-2.0
#
# Host-side unit tests for d126_hil.py, d126_utest.py and the shared
# helpers.  No hardware and no vendor tools are required.

import os
import tempfile
import unittest

import d126_hil as hil
import d126_utest
import hil_common
import utest_common


class FakeConsole:
    """Minimal Console stand-in: one canned response per command."""

    def __init__(self, responses, prompt=True):
        self.responses = list(responses)
        self.buffer = ""
        self.pending = ""
        self.prompt = prompt
        self.closed = False

    def send(self, line):
        self.pending = self.responses.pop(0) if self.responses else ""

    def read_once(self):
        if self.pending:
            self.buffer += self.pending
            self.pending = ""
            return self.buffer
        return ""

    def drain(self, seconds):
        pass

    def read_until(self, needles, timeout):
        self.read_once()
        for needle in needles:
            if needle in self.buffer:
                return needle
        return None

    def wait_prompt(self, timeout):
        self.buffer = ""
        return self.prompt

    def close(self):
        self.closed = True


def prompt(text):
    return text + "\r\nmsh >\r\n"


class ClassifyTest(unittest.TestCase):
    def test_pass(self):
        result = {"status": "RUN", "seconds": 0.1,
                  "output": prompt("d126_tick: delay=100 tick=100 "
                                   "expected=100 PASS")}
        entry = hil._classify("d126_tick 100", result,
                              hil.re.compile(r"d126_tick:.*\bPASS\b"))
        self.assertEqual(entry["status"], "PASS")

    def test_fail(self):
        result = {"status": "RUN", "seconds": 0.1,
                  "output": prompt("d126_tick: delay=100 tick=2 "
                                   "expected=100 FAIL")}
        entry = hil._classify("d126_tick 100", result,
                              hil.re.compile(r"d126_tick:.*\bPASS\b"))
        self.assertEqual(entry["status"], "FAIL")

    def test_missing_command_is_skip(self):
        result = {"status": "RUN", "seconds": 0.1,
                  "output": prompt("d126_sched: command not found")}
        entry = hil._classify("d126_sched 1", result,
                              hil.re.compile(r"d126_sched:.*\bPASS\b"))
        self.assertEqual(entry["status"], "SKIP")

    def test_timeout_and_reset(self):
        for status in ("TIMEOUT", "RESET"):
            result = {"status": status, "seconds": 1, "output": ""}
            entry = hil._classify("x", result, None)
            self.assertEqual(entry["status"], status)

    def test_board_identity_regex(self):
        result = {"status": "RUN", "seconds": 0.1,
                  "output": prompt("d126_info: board=d126-demo124-nor "
                                   "cpu=e907f")}
        entry = hil._classify(
            "d126_info", result, None,
            expect_board=r"d126_info: board=d126-demo124-nor\b")
        self.assertEqual(entry["status"], "PASS")

        result["output"] = prompt("d126_info: board=other")
        entry = hil._classify(
            "d126_info", result, None,
            expect_board=r"d126_info: board=d126-demo124-nor\b")
        self.assertEqual(entry["status"], "FAIL")


class SummarizeTest(unittest.TestCase):
    def _iter(self, name, statuses):
        return {"name": name, "required": True,
                "iterations": [{"status": s} for s in statuses]}

    def test_pass(self):
        tests = [self._iter(n, ["PASS", "PASS"])
                 for n in ("tick", "sched", "ipc", "mem")]
        self.assertEqual(hil.summarize_m1(tests), ("PASS", []))

    def test_incomplete_when_skipped(self):
        tests = [self._iter(n, ["PASS"]) for n in ("sched", "ipc", "mem")]
        tests.append(self._iter("tick", ["SKIP"]))
        status, detail = hil.summarize_m1(tests)
        self.assertEqual(status, "INCOMPLETE")
        self.assertEqual(detail, ["tick"])

    def test_fail_on_reset(self):
        tests = [self._iter(n, ["PASS"]) for n in ("tick", "sched", "ipc")]
        tests.append(self._iter("mem", ["RESET"]))
        status, detail = hil.summarize_m1(tests)
        self.assertEqual(status, "FAIL")
        self.assertEqual(detail, ["mem"])


class BoardIdentityTest(unittest.TestCase):
    def test_strong(self):
        console = FakeConsole([
            prompt("d126_info: board=d126-demo124-nor cpu=e907f"),
        ])
        self.assertEqual(hil.board_identity(console)["strength"], "strong")

    def test_weak_old_firmware(self):
        console = FakeConsole([
            prompt("d126_info: command not found"),
            prompt("memtest: 1 KiB @ 0x40011728: PASS"),
        ])
        self.assertEqual(hil.board_identity(console)["strength"], "weak")

    def test_unknown(self):
        console = FakeConsole([
            prompt("d126_info: command not found"),
            prompt("d126_memtest: command not found"),
        ])
        self.assertEqual(hil.board_identity(console)["strength"], "unknown")


class M1SuiteTest(unittest.TestCase):
    def test_pass_and_build_id(self):
        build_id = "d126fxr-demo-v1-1-abc-123"
        console = FakeConsole([
            prompt("d126_info: board=d126-demo124-nor cpu=e907f"),
            prompt("- RT - Thread Operating System"),
            prompt("build_id: %s\ngit_sha: abc\ntarget: "
                   "d126fxr-demo-v1-1" % build_id),
            prompt("d126_tick: delay=100 tick=100 expected=100 PASS"),
            prompt("d126_sched: loops=500 counter=1000 expected=1000 PASS"),
            prompt("d126_ipc: sem=PASS mb=PASS PASS"),
            prompt("memtest: 1024 KiB @ 0x40011728: PASS"),
        ])
        got_id, tests = hil.m1_suite(console, 1, 1)
        self.assertEqual(got_id, build_id)
        self.assertEqual(hil.summarize_m1(tests), ("PASS", []))

    def test_expect_build_id_mismatch(self):
        console = FakeConsole([
            prompt("d126_info: board=d126-demo124-nor"),
            prompt("- RT - Thread Operating System"),
            prompt("build_id: old"),
            prompt("d126_tick: PASS"),
            prompt("d126_sched: PASS"),
            prompt("d126_ipc: PASS"),
            prompt("memtest: PASS"),
        ])
        _, tests = hil.m1_suite(console, 1, 1, expect_build_id="new")
        self.assertEqual(hil.summarize_m1(tests)[0], "FAIL")

    def test_console_dead_stops_early(self):
        console = FakeConsole([
            prompt("d126_info: board=d126-demo124-nor"),
            prompt("- RT - Thread Operating System"),
            prompt("build_id: x"),
        ])
        _, tests = hil.m1_suite(console, 3, 0.05)
        named = {t["name"]: t for t in tests if "iterations" in t}
        self.assertEqual(len(named["tick"]["iterations"]), 1)
        self.assertEqual(named["tick"]["iterations"][0]["status"], "TIMEOUT")
        self.assertEqual(named["ipc"]["iterations"], [])
        self.assertEqual(hil.summarize_m1(tests)[0], "FAIL")


class SerialModeTest(unittest.TestCase):
    def _responses(self):
        return [
            # board_identity() runs once before the M1 suite, and the
            # suite itself starts with d126_info again.
            prompt("d126_info: board=d126-demo124-nor cpu=e907f"),
            prompt("d126_info: board=d126-demo124-nor cpu=e907f"),
            prompt("- RT - Thread Operating System"),
            prompt("build_id: d126fxr-demo-v1-1-abc-123"),
            prompt("d126_tick: PASS"),
            prompt("d126_sched: PASS"),
            prompt("d126_ipc: PASS"),
            prompt("memtest: PASS"),
        ]

    def _args(self):
        return type("Args", (), {
            "port": "COM_TEST", "baud": 115200, "prompt_timeout": 1,
            "repeat": 1, "cmd_timeout": 1, "expect_build_id": None})()

    def test_pass(self):
        console = FakeConsole(self._responses())
        old = hil.open_console
        hil.open_console = lambda *a, **k: console
        try:
            with tempfile.TemporaryDirectory() as tmp:
                report = {"stages": {}, "firmware": {}, "tests": []}
                status, error = hil.run_serial_mode(
                    self._args(), report, tmp)
        finally:
            hil.open_console = old
        self.assertEqual(status, "PASS")
        self.assertIsNone(error)
        self.assertEqual(report["firmware"]["build_id"],
                         "d126fxr-demo-v1-1-abc-123")

    def test_no_prompt(self):
        console = FakeConsole([], prompt=False)
        old = hil.open_console
        hil.open_console = lambda *a, **k: console
        try:
            with tempfile.TemporaryDirectory() as tmp:
                report = {"stages": {}, "firmware": {}, "tests": []}
                status, _ = hil.run_serial_mode(self._args(), report, tmp)
        finally:
            hil.open_console = old
        self.assertEqual(status, "NO_PROMPT")


def reboot_response(banner=True):
    text = ("msh >reboot\r\n"
            "reboot: WRI reason 1 (command reboot) latched and verified, "
            "resetting\r\n"
            "[bootloader log]\r\n")
    if banner:
        text += "ArtInChip D126 (D12p) RT-Thread BSP\r\nmsh >\r\n"
    return text


def smoke_responses(build_id="d126fxr-demo-v1-1-abc-123"):
    return [
        prompt("d126_info: board=d126-demo124-nor cpu=e907f"),
        prompt("- RT - Thread Operating System"),
        prompt("build_id: %s\ngit_sha: abc" % build_id),
        prompt("d126_tick: delay=100 tick=100 expected=100 PASS"),
        prompt("d126_sched: loops=500 counter=1000 expected=1000 PASS"),
        prompt("d126_ipc: sem=PASS mb=PASS PASS"),
        prompt("memtest: 1024 KiB @ 0x40011728: PASS"),
    ]


class StabilityTest(unittest.TestCase):
    def _args(self, **overrides):
        values = {
            "port": "COM_TEST", "baud": 115200, "prompt_timeout": 1,
            "cmd_timeout": 1, "boot_timeout": 1, "cycles": 2,
            "duration": 0, "response_limit": 5.0, "heap_drift": 16384,
            "expect_build_id": None,
        }
        values.update(overrides)
        return type("Args", (), values)()

    def test_parse_free(self):
        values = hil.parse_free("total memory: 8388608\r\n"
                                "used memory: 12345\r\n"
                                "maximum alloc memory: 4096\r\n")
        self.assertEqual(values, {"total": 8388608, "used": 12345})
        values = hil.parse_free("total    : 8289536\r\n"
                                "used     : 5096\r\n"
                                "maximum  : 1053688\r\n"
                                "available: 8284440\r\n")
        self.assertEqual(values, {"total": 8289536, "used": 5096})

    def test_cycle_pass(self):
        console = FakeConsole([reboot_response()] + smoke_responses())
        entry = hil.stability_cycle(console, self._args(), 1)
        self.assertEqual(entry["status"], "PASS")
        self.assertEqual(entry["boot"]["status"], "BOOT")
        self.assertEqual(entry["build_id"], "d126fxr-demo-v1-1-abc-123")
        self.assertIn("msh_response_s", entry)

    def test_cycle_boot_timeout(self):
        console = FakeConsole([reboot_response(banner=False)])
        entry = hil.stability_cycle(console, self._args(), 1)
        self.assertEqual(entry["status"], "BOOT_TIMEOUT")

    def test_cycle_slow_msh_fails(self):
        console = FakeConsole([reboot_response()] + smoke_responses())
        entry = hil.stability_cycle(
            console, self._args(response_limit=-1), 1)
        self.assertEqual(entry["status"], "FAIL")
        self.assertIn("msh response", entry["detail"][0])

    def test_mode_pass(self):
        responses = [
            prompt("d126_info: board=d126-demo124-nor cpu=e907f"),
            prompt("build_id: d126fxr-demo-v1-1-abc-123\ngit_sha: abc"),
        ]
        responses += [reboot_response()] + smoke_responses()
        responses += [reboot_response()] + smoke_responses()
        console = FakeConsole(responses)
        old = hil.open_console
        hil.open_console = lambda *a, **k: console
        try:
            with tempfile.TemporaryDirectory() as tmp:
                report = {"stages": {}, "firmware": {}, "tests": []}
                status, error = hil.run_stability_mode(
                    self._args(), report, tmp)
        finally:
            hil.open_console = old
        self.assertEqual(status, "PASS")
        self.assertIsNone(error)
        self.assertEqual(report["stability"]["cycles_run"], 2)
        self.assertEqual(console.closed, True)

    def test_mode_stops_at_first_bad_boot(self):
        responses = [
            prompt("d126_info: board=d126-demo124-nor cpu=e907f"),
            prompt("build_id: x"),
            reboot_response(banner=False),
        ]
        console = FakeConsole(responses)
        old = hil.open_console
        hil.open_console = lambda *a, **k: console
        try:
            with tempfile.TemporaryDirectory() as tmp:
                report = {"stages": {}, "firmware": {}, "tests": []}
                status, _ = hil.run_stability_mode(
                    self._args(cycles=5), report, tmp)
        finally:
            hil.open_console = old
        self.assertEqual(status, "TEST_FAIL")
        self.assertEqual(report["stability"]["cycles_run"], 1)

    def test_long_run_one_iteration(self):
        console = FakeConsole([
            prompt("d126_tick: PASS"),
            prompt("d126_sched: PASS"),
            prompt("d126_ipc: PASS"),
            prompt("memtest: PASS"),
        ])
        result = hil.long_run(console, self._args(duration=0))
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["iterations"], 1)

    def test_long_run_failure(self):
        console = FakeConsole([
            prompt("d126_tick: PASS"),
            prompt("d126_sched: counter=1 expected=2 FAIL"),
        ])
        result = hil.long_run(console, self._args(duration=0))
        self.assertEqual(result["status"], "FAIL")
        self.assertEqual(result["failures"][0]["command"], "d126_sched 100")


class DeviceSelectorTest(unittest.TestCase):
    TEXT = ("Detected 2 ArtInChip device(s):\n"
            "  bus=1 address=25 path=9-1 vid=0x33c3\n"
            "  bus=1 address=24 path=9-4 vid=0x33c3\n")

    def test_path(self):
        self.assertTrue(hil.selector_known("9-1", ["9-1", "9-4"],
                                           self.TEXT))
        self.assertFalse(hil.selector_known("9-2", ["9-1", "9-4"],
                                            self.TEXT))

    def test_bus_address(self):
        self.assertTrue(hil.selector_known("1:25", [], self.TEXT))
        self.assertFalse(hil.selector_known("1:99", [], self.TEXT))

    def test_json_order(self):
        text = ('{"devices":[{"address":25,"bus":1,"path":"9-1"}]}')
        self.assertTrue(hil.selector_known("1:25", [], text))


class ImageParseTest(unittest.TestCase):
    def test_d12p_image_info(self):
        text = (
            "=== Image Header ===\n"
            "  Product:     d126-demo124-nor\n"
            "=== META Entries (2) ===\n"
            "  [ 0] image.updater.psram (partition: ram)\n"
            "  [ 1] image.target.os (partition: os@spi-nor)\n"
        )
        old = hil_common.capture
        hil_common.capture = lambda cmd, timeout: (0, text)
        try:
            info = hil_common.parse_image_aicflash("flasher", "fw.img")
        finally:
            hil_common.capture = old
        self.assertEqual(info["product"], "d126-demo124-nor")
        self.assertEqual(info["components"], ["image.target.os"])

    def test_os_partition_plan(self):
        text = (
            "Burn plan:\n"
            "  target: image.target.spl partition=spl media=spi-nor "
            "size=1 selected=false\n"
            "  target: image.target.os partition=os media=spi-nor "
            "size=2 selected=true\n"
        )
        old = hil_common.capture
        hil_common.capture = lambda cmd, timeout: (0, text)
        try:
            selected, _ = hil_common.plan_os_partition("flasher", "fw.img")
        finally:
            hil_common.capture = old
        self.assertEqual(selected, ["image.target.os"])

    def test_os_partition_plan_unsupported(self):
        old = hil_common.capture
        hil_common.capture = lambda cmd, timeout: (2, "unexpected argument")
        try:
            selected, _ = hil_common.plan_os_partition("flasher", "fw.img")
        finally:
            hil_common.capture = old
        self.assertIsNone(selected)

    def test_os_target_trim_span(self):
        text = (
            '{\n  "image": {\n    "target": {\n'
            '      "spl": { "file": "bootloader.aic" },\n'
            '      "os": { "file": "d12p_os.itb" }\n    }\n  }\n}\n'
        )
        span = hil_common._json_object_span(text, "target")
        self.assertIsNotNone(span)
        section = text[span[0]:span[1]]
        self.assertIn('"os"', section)
        self.assertIn('"spl"', section)


class UtestDefaultsTest(unittest.TestCase):
    def test_no_fpu_in_d126_suites(self):
        joined = " ".join(d126_utest.DEFAULT_SUITES +
                          d126_utest.DEFAULT_KNOWN_FAIL)
        self.assertNotIn("fpu", joined.lower())

    def test_d126_suites(self):
        self.assertIn("bsp.d126.*", d126_utest.DEFAULT_SUITES)
        self.assertIn("core.irq", d126_utest.DEFAULT_SUITES)


class UtestExcludeTest(unittest.TestCase):
    def test_without_excludes_unchanged(self):
        suites = ["core.irq", "bsp.d126.*"]
        self.assertEqual(utest_common.expand_suites([], suites, []), suites)

    def test_expand_then_exclude(self):
        listed = ["core.irq", "bsp.d126.tick", "bsp.d126.ipc"]
        suites = ["core.irq", "bsp.d126.*"]
        selected = utest_common.expand_suites(
            listed, suites, ["bsp.d126.ipc"])
        self.assertEqual(selected, ["core.irq", "bsp.d126.tick"])

    def test_d213_non_fpu_selection(self):
        listed = ["bsp.d213.smoke", "bsp.d213.fpu.context"]
        suites = ["bsp.d213.*"]
        selected = utest_common.expand_suites(
            listed, suites, ["bsp.d213.fpu.*"])
        self.assertEqual(selected, ["bsp.d213.smoke"])


class LockTest(unittest.TestCase):
    def test_contention(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "x.lock")
            first = hil_common.FileLock(path)
            second = hil_common.FileLock(path)
            self.assertTrue(first.acquire())
            self.assertFalse(second.acquire())
            first.release()
            self.assertTrue(second.acquire())
            second.release()


if __name__ == "__main__":
    unittest.main()
