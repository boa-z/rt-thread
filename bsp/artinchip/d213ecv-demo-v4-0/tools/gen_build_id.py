#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# SPDX-License-Identifier: Apache-2.0
#
# D213 entry point for the shared ArtInChip Build ID generator.
# The implementation lives in bsp/artinchip/tools/gen_build_id.py so the
# D126 BSP can emit the same header without copying it.

import os
import subprocess
import sys

BSP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHARED = os.path.normpath(os.path.join(BSP, "..", "tools", "gen_build_id.py"))

if __name__ == "__main__":
    cmd = [sys.executable, SHARED, "--bsp-dir", BSP] + sys.argv[1:]
    sys.exit(subprocess.call(cmd))
