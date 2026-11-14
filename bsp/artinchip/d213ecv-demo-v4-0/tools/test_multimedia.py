#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run real buffer validation, VE and NAND driver logic with host RT/MMIO doubles.

No SDK, MPP, target compiler or board is needed. Pass --sanitize for ASan/UBSan.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sanitize', action='store_true')
a = p.parse_args()
bsp = Path(__file__).resolve().parents[1]
host = bsp / 'utest/host'
libraries = bsp / 'libraries' if (bsp / 'libraries').is_dir() else bsp.parent / 'libraries'
includes = libraries / 'soc/d21x/include'
with tempfile.TemporaryDirectory(prefix='d213-multimedia-') as output:
    for name in ('buffer', 've', 'board_layout', 'spinand'):
        cmd = [os.environ.get('CC', 'cc'), '-std=c11', '-g', '-Wall', '-Wextra', '-Werror',
               '-I' + str(host / 'include'), '-I' + str(includes),
               '-I' + str(includes.parent / 'drivers'),
               '-I' + str(libraries / 'include')]
        if name == 'spinand':
            kernel = bsp / 'rt-thread' if (bsp / 'rt-thread').is_dir() else bsp.parents[2]
            cmd += ['-I' + str(kernel / 'components/drivers/include/drivers'),
                    '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections']
        if a.sanitize:
            cmd += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        executable = Path(output) / name
        subprocess.run(cmd + [str(host / ('test_' + name + '.c')), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True, timeout=15)
        print(name + ': PASS', flush=True)
