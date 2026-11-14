#!/usr/bin/env python3
"""Check native Kconfig/header consistency and optionally build the base BSP."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import kconfiglib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bsp', type=Path, required=True,
                        help='path to the upstream base BSP directory')
    parser.add_argument('--minimal', action='store_true',
                        help='also require the UART0-only default feature set')
    parser.add_argument('--build', action='store_true',
                        help='run scons after successful configuration checks')
    args = parser.parse_args()
    bsp = args.bsp.resolve()
    os.chdir(bsp)
    rtt = Path(os.getenv('RTT_ROOT') or
               (bsp / 'rt-thread' if (bsp / 'rt-thread').is_dir() else bsp / '../../..'))
    sys.path.insert(0, str(rtt.resolve() / 'tools'))
    from env_utility import mk_rtconfig

    kconf = kconfiglib.Kconfig('Kconfig')
    kconf.load_config('.config')
    if kconf.warnings:
        raise RuntimeError('\n'.join(kconf.warnings))

    with tempfile.TemporaryDirectory(prefix='d213-config-') as tmp:
        config = Path(tmp) / '.config'
        kconf.write_config(str(config))
        if config.read_bytes() != (bsp / '.config').read_bytes():
            raise RuntimeError('.config is stale; run scons --pyconfig-silent')
        os.chdir(tmp)
        try:
            mk_rtconfig(str(config))
        finally:
            os.chdir(bsp)
        if (Path(tmp) / 'rtconfig.h').read_bytes() != (bsp / 'rtconfig.h').read_bytes():
            raise RuntimeError('rtconfig.h differs from the native configuration generator')

    if args.minimal:
        required = ('SOC_D213', 'AIC_SOC_D21X', 'ARCH_RISCV_M_MODE', 'ARCH_RISCV_FPU_D',
                    'RT_USING_RISCV_NEW_COMMON', 'BSP_USING_UART0',
                    'RT_USING_SERIAL_V1', 'RT_USING_MSH', 'RT_USING_HEAP')
        forbidden = ('RT_USING_SERIAL_V2', 'RT_SERIAL_USING_DMA', 'RT_USING_PIN',
                     'RT_USING_I2C', 'RT_USING_CAN', 'RT_USING_PWM', 'RT_USING_RTC',
                     'RT_USING_WDT', 'RT_USING_TOUCH', 'RT_USING_DFS', 'RT_USING_LWIP',
                     'RT_USING_SDIO', 'RT_USING_SPI', 'RT_USING_CHERRYUSB',
                     'RT_USING_AUDIO', 'RT_USING_UTESTCASES', 'RT_USING_SMP')
        for name, expected in [(s, 'y') for s in required] + [(s, 'n') for s in forbidden]:
            symbol = kconf.syms.get(name)
            if symbol is None or symbol.str_value != expected:
                raise RuntimeError('{} must be {}'.format(name, expected))
        if kconf.syms['RT_CONSOLE_DEVICE_NAME'].str_value != 'uart0':
            raise RuntimeError('minimal console must be uart0')

    print('D213 configuration and generated header are consistent.')
    if args.build:
        subprocess.run(['scons', '-j8'], check=True)


if __name__ == '__main__':
    main()
