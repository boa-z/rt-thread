#
# SPDX-License-Identifier: Apache-2.0
#
# scons --dist hook shared by the ArtInChip BSPs.
#
# The board directories keep the shared HAL in ../libraries so that one copy
# serves every ArtInChip board.  dist flattens the tree, so copy the shared
# directory next to the board and rewrite the paths that pointed outside it.
#

import os
import shutil


def selected_soc_layers(bsp_root):
    """Return the lowercase SoC layer names selected by the board config."""
    config = os.path.join(bsp_root, '.config')
    selected = set()
    if not os.path.isfile(config):
        return selected

    prefix = 'CONFIG_AIC_SOC_'
    with open(config, 'r') as f:
        for line in f:
            line = line.strip()
            if line.startswith(prefix) and line.endswith('=y'):
                selected.add(line[len(prefix):-2].lower())
    return selected


def update_dist_project_paths(dist_dir):
    for name in ('SConstruct', 'rtconfig.py'):
        path = os.path.join(dist_dir, name)
        if not os.path.isfile(path):
            continue

        with open(path, 'r') as f:
            data = f.read()

        data = data.replace(
            "os.path.join(os.getcwd(), '..', '..', '..')",
            "os.path.join(os.getcwd(), 'rt-thread')")

        with open(path, 'w') as f:
            f.write(data)

    kconfig_path = os.path.join(dist_dir, 'Kconfig')
    if os.path.isfile(kconfig_path):
        with open(kconfig_path, 'r') as f:
            data = f.read()

        data = data.replace('rsource "../libraries/Kconfig"',
                            'rsource "libraries/Kconfig"')

        with open(kconfig_path, 'w') as f:
            f.write(data)

    # The flattened project has no sibling ArtInChip tools directory.
    # D126 calls the shared generator directly; D213's local wrapper is
    # replaced with the standalone generator by dist_do_building().
    app_script = os.path.join(dist_dir, 'applications', 'SConscript')
    if os.path.isfile(app_script):
        with open(app_script, 'r') as f:
            data = f.read()
        data = data.replace("os.path.join(cwd, '..', '..', 'tools',",
                            "os.path.join(cwd, '..', 'tools',")
        with open(app_script, 'w') as f:
            f.write(data)


def dist_do_building(BSP_ROOT, dist_dir):
    from mkdist import bsp_copy_files

    print('=> copy ArtInChip shared libraries')
    library_path = os.path.join(os.path.dirname(BSP_ROOT), 'libraries')
    dist_library = os.path.join(dist_dir, 'libraries')
    os.makedirs(dist_library, exist_ok=True)

    # SoC-independent parts are always shipped.
    for name in ('include', 'hal', 'drivers'):
        source = os.path.join(library_path, name)
        if os.path.isdir(source):
            bsp_copy_files(source, os.path.join(dist_library, name))
    for name in ('Kconfig', 'SConscript', 'sdk_sources.json'):
        source = os.path.join(library_path, name)
        if os.path.isfile(source):
            shutil.copy2(source, os.path.join(dist_library, name))

    # Only the SoC layer compiled by this board belongs in its dist tree.
    selected = selected_soc_layers(BSP_ROOT)
    soc_path = os.path.join(library_path, 'soc')
    if os.path.isdir(soc_path):
        for name in sorted(os.listdir(soc_path)):
            source = os.path.join(soc_path, name)
            if os.path.isdir(source) and name in selected:
                print('   + libraries/soc/%s' % name)
                bsp_copy_files(source, os.path.join(dist_library, 'soc', name))
    if selected:
        print('   selected SoC layer(s): %s' % ', '.join(sorted(selected)))
    else:
        print('   no AIC_SOC_* layer selected; libraries/soc not copied')

    # Build identity generation must also work without the source checkout.
    os.makedirs(os.path.join(dist_dir, 'tools'), exist_ok=True)
    shutil.copy2(os.path.join(os.path.dirname(__file__), 'gen_build_id.py'),
                 os.path.join(dist_dir, 'tools', 'gen_build_id.py'))

    update_dist_project_paths(dist_dir)
