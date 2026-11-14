# SPDX-License-Identifier: Apache-2.0
"""D213 SPI-NAND SPL page-table capacity regression checks."""
import tempfile
from pathlib import Path
import unittest

from d213_mk_firmware import validate_spl_size


class SplSizeTest(unittest.TestCase):
    def test_nand_page_table_capacity(self):
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / 'bootloader.aic'
            for size, expected in ((407344, 200), (411648, 202)):
                with self.subTest(size=size):
                    with image.open('wb') as stream:
                        stream.truncate(size)
                    self.assertEqual(validate_spl_size(image), expected)
            for size in (0, 411649, 412688):
                with self.subTest(size=size):
                    with image.open('wb') as stream:
                        stream.truncate(size)
                    with self.assertRaises(ValueError):
                        validate_spl_size(image)


if __name__ == '__main__':
    unittest.main()
