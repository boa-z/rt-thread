# ArtInChip D213EC-DEMO-V4-0 BSP

Standard RT-Thread 5.3 board BSP for the ArtInChip D213 (D21x)
D213EC-DEMO-V4-0 board, using the `demo128-nand` software configuration.

- SoC: ArtInChip D213 (D21x family)
- CPU: T-Head C906, RV64GC (IMAFDC), machine mode
- Board: D213EC-DEMO-V4-0 (demo128-nand configuration), 128 MB DDR3, 128 MB SPI NAND
- Kernel: RT-Thread v5.3.0 with the Next RISC-V common port (PR #10320)
- Upstream baseline: `89c70abccd0359f9f7ba9df2e81dbe764d3e78d3`
- CPU port: `libcpu/risc-v/common` M-mode common startup/trap/timer
- Console: UART0, PA.0/PA.1, 115200 8N1
- Firmware load address / entry: `0x40000000`

## Status

| Area | State |
|---|---|
| M-mode startup, PLIC, CLINT, scheduler, IPC, FinSH | HW: MSH input, tick, scheduler and IPC verified; FPU context known-fail on the pure BSP |
| UART0 console | HW: 1 ms polled RX, MSH input and command output OK |
| UART1/2/4 multi-instance | BUILD + STATIC, hardware pending |
| GPIO / pin IRQ | BUILD + STATIC, hardware pending |
| I2C0-3 master | I2C3 HW PASS through GT911; I2C0/1/2 pending |
| CAN0/1 classic CAN 2.0A/B | Controller init/baud/mode PASS; loopback requires external TX/RX wiring; external bus SKIP (no second node) |
| PWM (4 channels) | BUILD + STATIC, hardware pending |
| WDT, RTC | HW PASS |
| GT911 capacitive touch (I2C3, PA.8/PA.9) | HW: five-point DOWN/MOVE/UP coordinates OK; orientation calibration pending |
| LVDS + DE framebuffer, color bar, VSync | HW: 1024x600, fb0/fb1 pan, VSync 10/10, underflow 0 |
| GE Normal fill/blit | HW: FillRect and BitBlt PASS |
| GE Normal scale/blend/rotate | BUILD + STATIC, re-test pending |
| LVGL 9.6.0 display + GT911 pointer input | HW: software render, click, drag and touch coordinates verified |
| D213 utest firmware | BUILD + STATIC, P0/P1 hardware regression pending |
| SPI NAND (QSPI0) + standard MTD, read-only | BUILD + STATIC + HOST PASS; HW PASS for JEDEC ID, geometry, status/ECC read, bad-block check, 100x page reread, MTD registration and write rejection. `aic_nand_partitions`/`os` FIT, cold/hot reboot and full peripheral regression still pending |
| SPI NAND write, NFTL, filesystem, OTA | Not implemented |
| MIPI/RGB, USB, SDIO, Ethernet, audio, video | Not implemented |

**CAN FD is not implemented and is not supported by the D21x controller.**
Do not enable the vendor `canfd` driver for D21x.

Build verification with Xuantie GCC 14.3.0 (V3.3.0), Python 3.13 and SCons 4.8.1:

| Configuration | text | data | bss |
|---|---:|---:|---:|
| `configs/d213-minimal.config` | 64632 | 2616 | 14232 |
| `configs/d213-default.config` (peripherals + display + GE + GT911) | 114360 | 5936 | 15416 |
| `configs/d213-utest.config` (default + utest) | 155968 | 6344 | 18360 |
| default + lvgl-aic-standalone software path | not rebuilt in this migration | - | - |

With Xuantie V2.6.1 / GCC 10.2.0 (the Luban-Lite toolchain used for the
storage port), the same profiles build to:

| Configuration | text | data | bss |
|---|---:|---:|---:|
| `configs/d213-minimal.config` | 62896 | 2616 | 14288 |
| `configs/d213-default.config` | 111384 | 5936 | 15408 |
| `configs/d213-utest.config` (with the NAND read-only case) | 175416 | 6344 | 20096 |
| `configs/d213-nand-readonly.config` | 82056 | 2616 | 16032 |

The first table was measured with GCC 14.3.0; the second table with the
Luban-Lite Xuantie GCC 10.2.0 toolchain. Object sizes differ because the two
toolchains use different code generation. Display/GE hardware verification
was done with an earlier GCC 10.2.0 display-only build; the new default image
includes the additional peripherals and needs a re-test.

Packaging with the current SDK assets:

| Configuration | `rtthread.bin` | `d21x_os.itb` | final `.img` |
|---|---:|---:|---:|
| minimal | 67384 | 69632 | 919040 |
| default (with GT911) | 120368 | 122880 | 972288 |
| utest | 162504 | 165888 | 1015296 |

Size alone is not hardware validation.

See `docs/README.md` for the full Chinese documentation set, including the
hardware support matrix, boot flow, peripheral manuals, CAN limitations,
build/packaging, testing, technical decisions and vendor license records.

## Build

Requirements:

- RISC-V GNU toolchain with `riscv64-unknown-elf-*`
  (tested with Xuantie-900 V2.6.1 / GCC 10.2.0 and V3.3.0 / GCC 14.3.0)
- Python 3 + SCons + kconfiglib
  (tested with Python 3.13, SCons 4.8.1, kconfiglib 14.1.0)

```sh
cd bsp/artinchip/d213-demo128-nand
export RTT_ROOT=/path/to/rt-thread
export RTT_EXEC_PATH=/path/to/toolchain/bin
export RTT_CC=gcc
scons -j8
```

`menuconfig`, `pyconfig`, `pyconfig-silent`, `--help`, `-c` and `-n` do not
invoke the cross compiler. `link.lds` is generated only when an actual
build/link runs, from `board/link.lds.S` and the current Kconfig output.

The tracked `.config` is the D213EC-DEMO-V4-0 board default: UART0/1, GPIO,
I2C3, CAN0/1, PWM, WDT, RTC, 1024x600 LVDS/DE framebuffer with double
buffering, GE Normal, and the peripheral/display/GE MSH test commands.
Unused instances remain available but disabled in Kconfig. Change the
configuration with:

```sh
scons --menuconfig
scons --pyconfig-silent
scons -j8
```

LVGL 9.6.0 is disabled by default and is not vendored. The BSP keeps only
an integration wrapper; the native port lives in the external
`lvgl-aic-standalone` repository. Enable `BSP_USING_LVGL` in menuconfig,
then build with:

```sh
git clone --branch v9.6.0 --depth 1 \
    https://github.com/lvgl/lvgl.git /path/to/lvgl-9.6.0
git clone https://github.com/boa-z/lvgl-aic-standalone.git \
    /path/to/lvgl-aic-standalone
export LVGL_DIR=/path/to/lvgl-9.6.0
export LVGL_AIC_DIR=/path/to/lvgl-aic-standalone
scons --pyconfig-silent
scons -j8
```

The standalone component includes GT911 pointer input, lifecycle/runtime
and the `lvgl_demo` / `lvgl_touch` MSH verification commands. Software frame
copy is the default; GE frame copy remains optional.

`rtconfig.h` is generated from `.config`; do not edit it by hand.

## SPI NAND (read-only)

`configs/d213-nand-readonly.config` enables QSPI0, the SPI NAND driver and
the standard RT-Thread MTD NAND device:

```sh
cp configs/d213-nand-readonly.config .config
scons --pyconfig-silent
scons -j8
```

The partition table is generated from `pack/image_cfg.json` at build time
(`tools/gen_nand_parts.py`); do not add a second copy of the offsets.  Only
linear partitions are registered as MTD devices.  `data` and `data_r` are
NFTL-backed and are intentionally not exposed as raw linear devices.

MSH commands (all read-only):

```text
aic_nand_info
aic_nand_status
aic_nand_read_page <physical_page>
aic_nand_check_block <physical_block>
aic_nand_partitions
aic_nand_read_part <name> <offset> <length>
```

Notes:

- `read_page` / `check_block` use whole-chip physical numbers;
- `read_part` uses a partition-relative byte offset and a length of at most
  4096 bytes;
- `read_page` reports `clean`, `corrected`, `UNCORRECTABLE`, `timeout` or
  `transport error`;
- program, erase, move and mark-bad are rejected with `-RT_EPERM`; there is no
  MSH write/erase command;
- generic `rt_device_read()` / `rt_device_write()` on an MTD partition return
  `-RT_ENOSYS` / `-RT_EPERM`; use `rt_mtd_nand_read()` for page data;
- partition reads are linear and do not skip bad blocks.  The Bootloader
  bad-block mapping is not confirmed, so `aic_nand_partitions` reports the
  bad-block count per linear partition and `os` is only claimed readable
  when its FIT magic and totalsize are valid.

See `docs/12-storage.md` for the full design, error semantics, provenance and
the hardware acceptance checklist.  Current status is BUILD + STATIC + HOST
PASS, plus HW PASS for JEDEC ID `EF AA 21`, geometry, status/ECC read,
bad-block check, 100x page reread, MTD registration and write rejection.
Partition/FIT output, cold/hot reboot and full peripheral regression remain
PENDING.

## Regression tests

The default and minimal images never link the test cases.  A separate
`configs/d213-utest.config` enables the standard RT-Thread utest framework,
the official uassert/kernel cases and the D213 cases under `utest/`.

Build and statically check the test firmware with the shared matrix script:

```sh
python bsp/artinchip/tools/build-check.py \
    --targets d213-utest \
    --sdk "$AIC_SDK_DIR"
```

The check verifies that the utest runtime symbols and the expected testcase
names are present in the ELF.  BUILD/STATIC/PACKAGE PASS is not a hardware
test.

Run the suites from the UART0 console:

```text
msh > utest_list
msh > utest_run -thread components.utilities.utest.uassert
msh > utest_run -thread core.irq
msh > utest_run -thread core.semaphore
msh > utest_run -thread core.mutex
msh > utest_run -thread core.mutex_pi
msh > utest_run -thread bsp.d213.*
```

`bsp.d213.spinand.ro` reads the `os` partition header 100 times, checks
bounds rejection, verifies the bad-block marker path and asserts that
write/erase/move/mark-bad all return `-RT_EPERM`.  It never modifies NAND.
If no NAND/MTD device is registered the case prints a skip message and
returns without failing the rest of the suite.

Or use the host-side serial runner (requires `pip install pyserial`):

```sh
python bsp/artinchip/tools/d213_utest.py --port COM7 \
    --log bsp/artinchip/artifacts/d213-utest-serial.log
```

`bsp.d213.fpu.context` is a known failure on the pure BSP because the
upstream common port does not preserve `fs0..fs11` across a thread switch.
The host script reports it as `KNOWN-FAIL`; do not hide it or patch
`libcpu/risc-v/common/` just to make the result green.  The integration
branch `integration/artinchip-fpu-test` is the place for the common-port
FPU fix.

See `docs/22-utest-regression.md` for the P0/P1/P2 case list, hardware
conditions, host script exit codes and the evidence-level rules.

## Firmware packaging

Packaging is a separate step and uses the original ArtInChip image tools and
the original D213 bootloader/DDR/PBP assets from a local Luban-Lite SDK. Use
the repository's uv virtual environment so the same interpreter works on
Windows, Linux and macOS:

```sh
cd /path/to/rt-thread
uv venv .venv
source .venv/bin/activate          # Windows: .venv\Scripts\activate
uv pip install -r tools/requirements.txt
uv pip install pycryptodomex
```

Then package from the BSP directory:

```sh
export AIC_SDK_DIR=/path/to/luban-lite-jc
python tools/mk_firmware.py --sdk "$AIC_SDK_DIR" \
    --elf rtthread.elf --out images
```

Windows PowerShell is the same after activation:

```powershell
$env:AIC_SDK_DIR = "C:\path\to\luban-lite-jc"
python tools\mk_firmware.py --sdk $env:AIC_SDK_DIR `
    --elf rtthread.elf --out images
```

`mk_firmware.py` calls the SDK scripts with the Python interpreter running
the command. If the venv is not activated, pass
`--python /path/to/rt-thread/.venv/bin/python` explicitly. The SDK's bundled
`tools/env/tools/Python38/python3.exe` is a Windows binary and is not selected
automatically on macOS or Linux.

The SDK must provide:

- `tools/scripts/mk_image.py`
- `tools/scripts/mkenvimage.py` (pure Python environment image builder)
- `tools/scripts/gen_partition_table.py`
- `tools/scripts/mk_private_resource.py`
- `target/d21x/demo128-nand/pack/bootloader.bin`
- `bsp/artinchip/sys/d21x/d21x.pbp`

The script never writes a device. It does not modify the bootloader, DDR
parameters or NAND partition layout. `env.bin` is byte-compatible with the
original U-Boot `mkenvimage`, including the `-r` redundant flag.

## Memory layout

```text
0x40000000  .text / .rodata / .data
            .bss
            interrupt stack
            heap
0x47800000  heap end
0x47B80000  original ArtInChip bootloader (reserved)
0x48000000  end of 128 MB DDR
```

The top 8 MB is reserved for the original bootloader. The image must fit in
the vendor `os` partition (about 4 MB in the demo128 layout).

## Directory layout

```text
applications/   boot entry, aic_selftest, FPU test, peripheral MSH commands
board/          common-port glue, clock, pinmux, PLIC quirk, timer, linker script
drivers/        RT-Thread adapters: UART/GPIO/I2C/CAN/PWM/WDT/RTC/DE/GE
utest/          D213 utest cases and integer context assembly
vendor/         curated ArtInChip HAL + minimal compatibility layer
lvgl/           optional LVGL 9.6.0 port, GT911 input and MSH demo
configs/        d213-minimal, d213-default and d213-utest profiles
docs/           complete Chinese documentation set
pack/           image/partition configuration
tools/          mk_firmware.py
```

## Source attribution

New BSP code is Apache-2.0. The files under `vendor/hal/` and the original
headers under `vendor/include/` are copied byte-for-byte from ArtInChip
Luban-Lite (Apache-2.0) and retain their original copyright notices. The
BSP-specific compatibility layer and clock provider are documented in
`docs/20-vendor-source-license.md`.

The CPU startup, trap, context switch, interrupt-controller adapter and
timer core are the upstream RT-Thread Next RISC-V common port. The D213 BSP
only supplies the C906 cache range helpers, the PLIC MMIO/PER quirk and the
D21x CLINT `rdtime`/`mtimecmp` accessors. No vendor binary is stored
in this BSP; the bootloader and PBP are supplied by the user's SDK at
packaging time.

## Limitations

- LVDS/DE framebuffer, color bar, double buffering, VSync and GE
  FillRect/BitBlt are hardware verified; GE scale/blend/rotate still need
  re-test.
- I2C3, WDT and RTC are hardware verified. GPIO, UART1/2/4 and PWM still
  need hardware validation.
- CAN requires an external transceiver, bus termination and a peer node.
  The D21x SELFTEST loopback also needs CAN0 TX/RX wired together; without
  that wiring the loopback utest remains disabled.
- SPI NAND read-only support exists (QSPI0 + standard MTD + partition/FIT
  diagnostics).  The QSPI0 link, W25N01GV identity, geometry, status/ECC read,
  bad-block check, 100x page reread, MTD registration and write rejection are
  hardware verified.  Partition/FIT output, cold/hot reboot and full
  peripheral regression remain to be tested.  There is no NAND
  write/erase, bad-block marking, NFTL, filesystem, OTA or refresh support.
  `data`/`data_r` remain NFTL-only and are not linear MTD devices.
- No MIPI/RGB, USB, SDIO, Ethernet, Wi-Fi, audio or video support yet.
- No general DMA framework or cache-coherent allocator.
- `libcpu/risc-v/common` is unmodified on this branch.  The pure BSP
  therefore reports the known `fs1` FPU context failure; the common-port
  fix lives on `integration/artinchip-fpu-test`.
