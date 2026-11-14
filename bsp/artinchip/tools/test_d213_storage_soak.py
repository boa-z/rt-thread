#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# SPDX-License-Identifier: Apache-2.0
#
# Host-side unit tests for the pure parsers in d213_storage_soak.py.

import unittest

import d213_storage_soak as soak


class ParseLfsInfoTest(unittest.TestCase):
    def test_parses_mounted_volume(self):
        text = (
            "nand_lfs_info\n"
            "device nand_scratch: 108 blocks of 131072 bytes\n"
            "blocks: 102 logical, 4 spare, 2 BBT, 0 bad\n"
            "BBT: seq 1, slots 0/1, active 0\n"
            "littlefs: mounted at /nand, used 2/102 blocks\n"
            "msh />"
        )
        info = soak.parse_lfs_info(text)
        self.assertEqual(info["sequence"], 1)
        self.assertEqual(info["slots"], [0, 1])
        self.assertEqual(info["active"], 0)
        self.assertTrue(info["mounted"])
        self.assertEqual(info["used_blocks"], 2)

    def test_unmounted_volume(self):
        info = soak.parse_lfs_info("BBT: seq 0, slots 0/1, active -1\n")
        self.assertEqual(info["active"], -1)
        self.assertFalse(info["mounted"])


class ParseBbtTestTest(unittest.TestCase):
    def test_parses_erased_slot(self):
        text = (
            "nand_lfs: BBT slot 0 (block 0, seq 1) erased; "
            "reboot to recover from slot 1\nmsh />"
        )
        result = soak.parse_bbt_test(text)
        self.assertEqual(result["slot"], 0)
        self.assertEqual(result["block"], 0)
        self.assertEqual(result["sequence"], 1)
        self.assertEqual(result["other_slot"], 1)

    def test_rejects_usage_text(self):
        self.assertIsNone(soak.parse_bbt_test("usage: nand_lfs_bbt_test"))


class ParseStressCheckTest(unittest.TestCase):
    def test_payload_ok(self):
        text = ("nand_lfs_stress: magic ok, seq 7, data 4096 bytes, "
                "payload ok\n")
        result = soak.parse_stress_check(text)
        self.assertEqual(result["sequence"], 7)
        self.assertEqual(result["data_size"], 4096)
        self.assertEqual(result["payload"], "ok")

    def test_payload_mismatch(self):
        text = ("nand_lfs_stress: magic ok, seq 7, data 4096 bytes, "
                "payload MISMATCH\n")
        self.assertEqual(soak.parse_stress_check(text)["payload"], "MISMATCH")

    def test_missing_file(self):
        text = "nand_lfs_stress: /nand/stress.bin is missing\n"
        self.assertEqual(soak.parse_stress_check(text), {"present": False})


class ParseOtaStatusTest(unittest.TestCase):
    def test_parses_env_and_slots(self):
        text = (
            "ArtInChip A/B slot status (read-only)\n"
            "  active  : env\n"
            "  osAB_now        : A\n"
            "  osAB_next       : A\n"
            "  FIT os  : valid\n"
            "  FIT os_r: no valid FIT header\n"
        )
        status = soak.parse_ota_status(text)
        self.assertEqual(status["active"], "env")
        self.assertEqual(status["osAB_now"], "A")
        self.assertEqual(status["osAB_next"], "A")
        self.assertEqual(status["fit_os"], "valid")
        self.assertEqual(status["fit_os_r"], "no valid FIT header")


class ParsePartitionsTest(unittest.TestCase):
    def test_os_slots(self):
        text = (
            "os      : FIT image detected (magic 0xD00DFEED, totalsize 729 "
            "bytes, partition 4194304 bytes)\n"
            "os_r    : no valid FIT header (magic 0xFFFFFFFF, totalsize 0, "
            "partition 4194304 bytes)\n"
        )
        parts = soak.parse_partitions(text)
        self.assertEqual(parts["os"], "valid")
        self.assertEqual(parts["os_r"], "empty")

    def test_read_error(self):
        text = "os      : header read failed (-5)\n"
        self.assertEqual(soak.parse_partitions(text)["os"], "read-error")


if __name__ == "__main__":
    unittest.main()
