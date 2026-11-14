# SPDX-License-Identifier: Apache-2.0
"""Pin exact imports while distinguishing the BSP's deliberate adaptations."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

import check_sdk_sources as checker


class SDKSourceTest(unittest.TestCase):
    def test_repository_imports(self):
        manifest = json.loads(checker.DEFAULT_MANIFEST.read_text())
        self.assertEqual(checker.audit(manifest, checker.ARTINCHIP), [])
        self.assertEqual(manifest['sdk']['version'], '1.3.3')

    def test_changes_and_missing_sources_are_detected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            local = root / 'local'
            sdk = root / 'sdk'
            local.mkdir(); sdk.mkdir()
            expected = hashlib.sha256(b'upstream').hexdigest()
            manifest = {'schema': 1, 'files': [
                {'source': 'exact.c', 'local': 'exact.c', 'mode': 'exact', 'sha256': expected},
                {'source': 'adapt.c', 'local': 'adapt.c', 'mode': 'adapted', 'sha256': expected},
            ]}
            for name in ('exact.c', 'adapt.c'):
                (sdk / name).write_bytes(b'upstream')
            (local / 'exact.c').write_bytes(b'upstream')
            (local / 'adapt.c').write_bytes(b'BSP adaptation')
            self.assertEqual(checker.audit(manifest, local, sdk), [])
            (sdk / 'exact.c').write_bytes(b'new SDK')
            (local / 'exact.c').write_bytes(b'accidental HAL edit')
            errors = checker.audit(manifest, local, sdk)
            self.assertEqual(len(errors), 2)
            self.assertTrue(any('exact import changed' in e for e in errors))
            self.assertTrue(any('pinned snapshot' in e for e in errors))
            (sdk / 'adapt.c').unlink()
            self.assertTrue(any('layout/version' in e for e in checker.audit(manifest, local, sdk)))
            (local / 'adapt.c').unlink()
            self.assertTrue(any('missing local source' in e for e in checker.audit(manifest, local)))
