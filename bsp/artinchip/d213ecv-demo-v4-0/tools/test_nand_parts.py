#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# SPDX-License-Identifier: Apache-2.0
#
# Host-side checks for the SPI NAND partition generator.
# These do not touch hardware; they verify the layout contract that the
# firmware build relies on.

import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import gen_nand_parts as gen


class NandPartsTest(unittest.TestCase):
    def test_real_layout(self):
        layout = gen.load_layout(gen.DEFAULT_IMAGE_CFG)
        gen.validate(layout)

        self.assertEqual(layout["page_size"], 2048)
        self.assertEqual(layout["oob_size"], 64)
        self.assertEqual(layout["block_size"], 128 * 1024)
        self.assertEqual(layout["total_blocks"], 1024)

        names = [p["name"] for p in layout["parts"]]
        self.assertEqual(names, [
            "spl", "env", "env_r", "os", "os_r", "rodata", "rodata_r",
            "data", "data_r", "refresh",
        ])

        parts = {p["name"]: p for p in layout["parts"]}
        self.assertEqual(parts["os"]["start_block"], 12)
        self.assertEqual(parts["os"]["block_count"], 32)
        self.assertEqual(parts["data"]["linear"], 0)
        self.assertEqual(parts["data_r"]["linear"], 0)
        self.assertEqual(parts["refresh"]["linear"], 1)

        end = 0
        for part in layout["parts"]:
            self.assertGreaterEqual(part["offset"], end)
            end = part["offset"] + part["size"]
        self.assertLessEqual(end, layout["total_size"])

    def test_unaligned_layout_is_rejected(self):
        cfg = {
            "spi-nand": {
                "size": "128m",
                "partitions": {"bad": {"size": "100k"}, "rest": {"size": "1m"}},
            },
            "image": {"info": {"media": {
                "type": "spi-nand", "device_id": 0,
                "array_organization": [{"page": "2k", "block": "128k",
                                        "oob": "64"}],
            }}},
        }
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False,
                                         encoding="utf-8") as f:
            json.dump(cfg, f)
            path = f.name
        try:
            with self.assertRaises(ValueError):
                gen.validate(gen.load_layout(path))
        finally:
            os.unlink(path)

    def test_overflow_is_rejected(self):
        cfg = {
            "spi-nand": {
                "size": "1m",
                "partitions": {"big": {"size": "2m"}},
            },
            "image": {"info": {"media": {
                "type": "spi-nand", "device_id": 0,
                "array_organization": [{"page": "2k", "block": "128k",
                                        "oob": "64"}],
            }}},
        }
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False,
                                         encoding="utf-8") as f:
            json.dump(cfg, f)
            path = f.name
        try:
            with self.assertRaises(ValueError):
                gen.validate(gen.load_layout(path))
        finally:
            os.unlink(path)

    def test_header_emission(self):
        layout = gen.load_layout(gen.DEFAULT_IMAGE_CFG)
        header = gen.emit_header(layout)
        self.assertIn("#define SPINAND_PART_COUNT %du" % len(layout["parts"]),
                      header)
        self.assertIn('{"os", 12u, 32u, 1u}', header)
        self.assertIn('{"data", 268u, 320u, 0u}', header)
        self.assertIn("#define SPINAND_CFG_TOTAL_BLOCKS   1024u", header)


if __name__ == "__main__":
    unittest.main()
