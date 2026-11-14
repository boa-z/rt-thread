# SPDX-License-Identifier: Apache-2.0
"""Distribution must build with local libraries and no sibling source tree."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

import sdk_dist


class DistributionTest(unittest.TestCase):
    def test_standalone_tools_and_soc_selection(self):
        for soc in ('d21x', None):
            with self.subTest(soc=soc), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                board = root / 'source' / 'board'
                board.mkdir(parents=True)
                (board / '.config').write_text('CONFIG_AIC_SOC_D21X=y\n' if soc else '')
                libraries = root / 'source' / 'libraries'
                for name in ('include', 'hal', 'drivers', 'soc/d21x', 'soc/other'):
                    directory = libraries / name
                    directory.mkdir(parents=True)
                    (directory / 'sentinel').touch()
                for name in ('Kconfig', 'SConscript', 'sdk_sources.json'):
                    (libraries / name).write_text('')
                dist = root / 'standalone'
                (dist / 'applications').mkdir(parents=True)
                (dist / 'applications' / 'SConscript').write_text(
                    "gen = os.path.join(cwd, '..', '..', 'tools', 'gen_build_id.py')\n")
                (dist / 'Kconfig').write_text('rsource "../libraries/Kconfig"\n')
                (dist / 'rtconfig.h').write_text('#define BOARD_TEST\n')
                copier = types.SimpleNamespace(bsp_copy_files=lambda src, dst:
                    shutil.copytree(src, dst, dirs_exist_ok=True))
                with patch.dict(sys.modules, {'mkdist': copier}):
                    sdk_dist.dist_do_building(str(board), str(dist))
                shutil.rmtree(root / 'source')
                self.assertEqual((dist / 'libraries/soc/d21x').exists(), soc == 'd21x')
                self.assertFalse((dist / 'libraries/soc/other').exists())
                self.assertTrue((dist / 'libraries/sdk_sources.json').exists())
                self.assertIn('rsource "libraries/Kconfig"', (dist / 'Kconfig').read_text())
                self.assertNotIn("'..', '..'", (dist / 'applications/SConscript').read_text())
                subprocess.run([sys.executable, str(dist / 'tools/gen_build_id.py'),
                                '--target', 'test', '--output', str(dist / 'identity.h')],
                               cwd=dist, check=True, capture_output=True)
                self.assertIn('test', (dist / 'identity.h').read_text())


if __name__ == '__main__':
    unittest.main()
