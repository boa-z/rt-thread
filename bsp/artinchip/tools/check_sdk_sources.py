#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Verify the pinned SDK source ledger without building or changing the SDK.

Without --sdk, checks exact imports and local adapted-file presence only.
With --sdk, also verifies each upstream source against its full SHA-256.
The current ledger describes Luban-Lite 1.x; 2.0 needs a separate source audit.
"""
import argparse
import hashlib
import json
from pathlib import Path

ARTINCHIP = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = ARTINCHIP / 'libraries/sdk_sources.json'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def audit(manifest, local_root, sdk=None):
    errors = []
    if manifest.get('schema') != 1:
        return ['unsupported SDK ledger schema']
    for entry in manifest['files']:
        if entry.get('mode') not in ('exact', 'adapted', 'reference'):
            errors.append('invalid source mode: ' + str(entry.get('mode')))
            continue
        if entry['mode'] != 'reference' and 'local' not in entry:
            errors.append('missing local path for ' + entry['source'])
            continue
        source = entry['source']
        expected = entry['sha256']
        local = Path(local_root) / entry['local'] if 'local' in entry else None
        if local is not None and not local.is_file():
            errors.append('missing local source: ' + str(local))
        elif local is not None and entry['mode'] == 'exact' and digest(local) != expected:
            errors.append('exact import changed: ' + str(local))
        if sdk is not None:
            upstream = Path(sdk) / source
            if not upstream.is_file():
                errors.append('missing SDK source (check SDK layout/version): ' + source)
            elif digest(upstream) != expected:
                errors.append('SDK source differs from pinned snapshot: ' + source)
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, help='read-only SDK snapshot directory')
    parser.add_argument('--manifest', type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument('--local-root', type=Path, default=ARTINCHIP)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding='utf-8'))
    errors = audit(manifest, args.local_root, args.sdk)
    for error in errors:
        print('FAIL: ' + error)
    print('SDK {} sources: {} ({} ledger entries)'.format(
        manifest['sdk']['version'], 'FAIL' if errors else 'PASS', len(manifest['files'])))
    return 1 if errors else 0


if __name__ == '__main__':
    raise SystemExit(main())
