#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# SPDX-License-Identifier: Apache-2.0
#
# Host-side unit tests for the pure helpers in d213_hil.py.

import json
import os
import tempfile
import unittest

import d213_hil as hil
import d213_utest
import utest_common


class FpuResultTest(unittest.TestCase):
    def test_fpu_failure_is_a_regression(self):
        result = {"status": "RUN", "passed": [],
                  "failed": ["bsp.d213.fpu.context"], "raw": ""}
        self.assertEqual(
            utest_common.classify(result, set(d213_utest.DEFAULT_KNOWN_FAIL)),
            ("FAIL", ["bsp.d213.fpu.context"]))

    def test_fpu_pass_is_accepted(self):
        result = {"status": "RUN", "passed": ["bsp.d213.fpu.context"],
                  "failed": [], "raw": ""}
        self.assertEqual(
            utest_common.classify(result, set(d213_utest.DEFAULT_KNOWN_FAIL)),
            ("PASS", []))


class ParseDevicesTest(unittest.TestCase):
    def test_parses_boot_stages(self):
        text = (
            "There are 2 USB device in list:\n"
            "Bus:Port 2:9-2\n"
            " - Boot stage: Boot ROM (D21xB)\n"
            "Bus:Port 2:9-3\n"
            " - Boot stage: Bootloader\n"
        )
        self.assertEqual(
            hil.parse_devices(text), ["Boot ROM (D21xB)", "Bootloader"])

    def test_no_device(self):
        self.assertEqual(hil.parse_devices("No usbupg device is found."), [])


class BuildIdTest(unittest.TestCase):
    def test_parses_build_id(self):
        text = (
            "aic_build_id\n"
            "build_id: d213ecv-demo-v4-0-abc-123\n"
            "git_sha: abc\n"
            "msh >\n"
        )
        self.assertEqual(
            hil.parse_build_id_output(text), "d213ecv-demo-v4-0-abc-123")

    def test_missing_build_id(self):
        self.assertIsNone(hil.parse_build_id_output("msh >"))


class ClassifyFlashTest(unittest.TestCase):
    def test_ambiguous(self):
        self.assertEqual(
            hil.classify_flash_failure("Multiple ArtInChip devices found"),
            "DEVICE_AMBIGUOUS")

    def test_not_found(self):
        self.assertEqual(
            hil.classify_flash_failure("No ArtInChip device found"),
            "DEVICE_NOT_FOUND")

    def test_interrupted(self):
        self.assertEqual(
            hil.classify_flash_failure("Bulk write failed: Pipe"),
            "FLASH_INTERRUPTED")

    def test_generic(self):
        self.assertEqual(
            hil.classify_flash_failure("Unknown target partition 'os'"),
            "FLASH_FAIL")


class ManifestTest(unittest.TestCase):
    def _with_temp_artifacts(self, func):
        old = hil.ARTIFACTS
        with tempfile.TemporaryDirectory() as tmp:
            hil.ARTIFACTS = tmp
            try:
                func(tmp)
            finally:
                hil.ARTIFACTS = old

    def test_load_manifest(self):
        def run(tmp):
            target_dir = os.path.join(tmp, hil.TARGET)
            os.makedirs(target_dir)
            with open(os.path.join(target_dir, "manifest.json"), "w",
                      encoding="utf-8") as f:
                json.dump({"build_id": "b", "img": {"path": "p"}}, f)
            data, path = hil.load_manifest(hil.TARGET)
            self.assertEqual(data["build_id"], "b")
            self.assertTrue(path.endswith("manifest.json"))

        self._with_temp_artifacts(run)

    def test_missing_manifest(self):
        def run(tmp):
            data, path = hil.load_manifest("missing")
            self.assertIsNone(data)

        self._with_temp_artifacts(run)

    def test_resolve_image_from_manifest(self):
        def run(tmp):
            image = os.path.join(tmp, "fw.img")
            with open(image, "wb") as f:
                f.write(b"firmware")
            target_dir = os.path.join(tmp, hil.TARGET)
            os.makedirs(target_dir)
            with open(os.path.join(target_dir, "manifest.json"), "w",
                      encoding="utf-8") as f:
                json.dump({
                    "build_id": "build-1",
                    "img": {"path": image,
                            "sha256": hil.sha256_file(image)},
                }, f)
            args = type("Args", (), {"image": None,
                                      "target": hil.TARGET})()
            resolved, sha, build_id, _, error = hil.resolve_image(args)
            self.assertEqual(resolved, image)
            self.assertEqual(sha, hil.sha256_file(image))
            self.assertEqual(build_id, "build-1")
            self.assertIsNone(error)

        self._with_temp_artifacts(run)

    def test_resolve_image_without_manifest_fails(self):
        def run(tmp):
            args = type("Args", (), {"image": None,
                                      "target": hil.TARGET})()
            image, _, _, _, error = hil.resolve_image(args)
            self.assertIsNone(image)
            self.assertIn("manifest", error)

        self._with_temp_artifacts(run)


class FileLockTest(unittest.TestCase):
    def test_contention_and_release(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "test.lock")
            first = hil.FileLock(path)
            second = hil.FileLock(path)
            self.assertTrue(first.acquire())
            self.assertFalse(second.acquire())
            first.release()
            self.assertTrue(second.acquire())
            second.release()


class ParseScanTest(unittest.TestCase):
    def test_parses_usb_path(self):
        text = (
            "Detected 1 ArtInChip device(s):\n"
            "  bus=1 address=4 path=9 vid=0x33c3 pid=0x6677 "
            "speed=High status=ready\n"
        )
        self.assertEqual(hil.parse_scan_devices(text), ["9"])

    def test_no_device(self):
        self.assertEqual(hil.parse_scan_devices("No devices found"), [])


class ParseAicflashImageTest(unittest.TestCase):
    def test_parses_product_and_components(self):
        text = (
            "=== Image Header ===\n"
            "  Product:     d213_demo128_page_2k_block_128k\n"
            "=== META Entries (2) ===\n"
            "  [ 0] image.target.env (partition: env@spi-nand)\n"
            "  [ 1] image.target.os (partition: os@spi-nand)\n"
        )
        old = hil.capture
        hil.capture = lambda cmd, timeout: (0, text)
        try:
            info = hil.parse_image_aicflash("artinchip-flash", "fw.img")
        finally:
            hil.capture = old
        self.assertEqual(info["product"],
                         "d213_demo128_page_2k_block_128k")
        self.assertEqual(info["components"],
                         ["image.target.env", "image.target.os"])


class UtestMappingTest(unittest.TestCase):
    def _run(self, rc, timeout=None):
        old_run, old_stage = hil.run_logged, hil.stage
        hil.run_logged = lambda *a, **k: (rc, timeout)
        hil.stage = lambda *a, **k: None
        args = type("Args", (), {
            "port": "COM9", "baud": 115200, "utest_timeout": 20,
            "suite": None, "known_fail": None})()
        try:
            with tempfile.TemporaryDirectory() as tmp:
                return hil.run_utest(args, {}, tmp)
        finally:
            hil.run_logged, hil.stage = old_run, old_stage

    def test_pass(self):
        self.assertEqual(self._run(0), "PASS")

    def test_known_fail(self):
        self.assertEqual(self._run(2), "TEST_KNOWN_FAIL")

    def test_test_fail(self):
        self.assertEqual(self._run(1), "TEST_FAIL")

    def test_timeout(self):
        self.assertEqual(self._run(0, "timeout"), "TEST_TIMEOUT")


class OpenConsoleTest(unittest.TestCase):
    def test_missing_port_raises(self):
        args = type("Args", (), {"port": "COM_DOES_NOT_EXIST",
                                 "baud": 115200})()
        with tempfile.TemporaryDirectory() as tmp:
            log = os.path.join(tmp, "serial.log")
            with self.assertRaises(RuntimeError):
                hil.open_console(args, log, timeout=0.3)


class SourceStateTest(unittest.TestCase):
    def test_returns_commit_and_status(self):
        commit, status = hil.source_state()
        self.assertEqual(len(commit), 40)
        self.assertIsInstance(status, str)


if __name__ == "__main__":
    unittest.main()
