# ArtInChip RT-Thread BSPs

本目录包含两个基于 RT-Thread **Next RISC-V common port**（PR #10320）
的 ArtInChip BSP：

```text
bsp/artinchip/
├── .gitignore           local artifacts/ and Python cache
├── artifacts/           ignored build outputs, images and HIL logs
├── d213-demo128-nand/   D213EC-DEMO-V4-0, C906 RV64GC, PLIC + CLINT
├── d126-demo124-nor/    D126FxR-DEMO-V1-1, E907F RV32, CLIC + CORET
└── tools/
    ├── check_elf.py     shared ELF/map checker
    ├── build-check.py   D213 minimal/default/utest + D126 matrix
    ├── d213_utest.py    read-only serial utest runner
    └── d213_hil.py      build/flash/utest hardware-in-the-loop runner
```

## 基线

```text
RT-Thread upstream master: 89c70abccd0359f9f7ba9df2e81dbe764d3e78d3
New common port:           PR #10320 "[ARCH/RISC-V] Next RISC-V"
```

两个 BSP 都设置：

```text
CONFIG_RT_USING_RISCV_NEW_COMMON=y
CONFIG_ARCH_RISCV_M_MODE=y
CONFIG_RT_USING_ADT=y
CONFIG_RT_USING_ADT_BITMAP=y
```

D213 use `ARCH_RISCV64` + `ARCH_RISCV_FPU_D`; D126 uses `ARCH_RISCV32` +
`ARCH_RISCV_FPU_S`.

## 快速构建

```bash
export RTT_EXEC_PATH=/opt/homebrew/bin

# 一键构建、静态检查、打包三个配置
python3 bsp/artinchip/tools/build-check.py \
  --sdk /path/to/luban-lite-jc
```

或单独构建：

```bash
cd bsp/artinchip/d213-demo128-nand
scons -j8
python3 tools/check_elf.py

cd ../d126-demo124-nor
scons -j8
python3 tools/check_elf.py
```

`build-check.py` 会：

1. 备份当前 `.config`/`rtconfig.h`；
2. 应用 `configs/*.config`；
3. 重新生成 `rtconfig.h`；
4. `scons -c` + `scons -j8`；
5. 运行共享 ELF/map 检查；
6. 打包（D213 真实 SDK 资源；D126 占位保护）；
7. 把 ELF/BIN/IMG 和日志放到 `bsp/artinchip/artifacts/`；
8. 生成 `bsp/artinchip/artifacts/manifest.md`；
9. 恢复原 `.config`/`rtconfig.h`。

`bsp/artinchip/artifacts/` 由 `bsp/artinchip/.gitignore` 排除，不进入源码历史。

## D213 HIL 自动化（构建 → 烧录 → utest）

`tools/d213_hil.py` 复用 `build-check.py`、`mk_firmware.py` 和
`d213_utest.py`，把构建、静态检查、打包、进入升级模式、OS-only 烧录、
RT-Thread 启动等待、Build ID 校验和 utest 串成一条流水线，输出
`report.json`（含 Git commit、镜像 SHA256、Build ID、串口日志和每个阶段
的状态）。

镜像通过 `artifacts/<target>/manifest.json` 选择（绝不按 mtime）：
manifest 记录 Build ID、Git SHA、config hash 以及 ELF/ITB/IMG 的
SHA256。烧录后串口执行 `aic_build_id`，与 manifest 不一致立即失败。

依赖：

- Python 3.8+、`pyserial`；
- Luban-Lite SDK 和 RISC-V 工具链（构建/打包）；
- `pycryptodomex`（SDK 的 `mk_image.py` 需要）；
- AiBurn v2.1.0 的 `upgcmd.exe`
  （`C:\Program Files\AiBurn\upgcmd.exe`），或带 `--partition` 的
  `artinchip-flash`（默认烧录后端；源码构建的二进制通过
  `--flasher-path` 指定）。

PowerShell 示例：

```powershell
# 1. 只读计划：构建/检查/打包 + 设备发现，不改变板子状态
python bsp\artinchip\tools\d213_hil.py --mode plan `
  --sdk C:\Project\D50T-2-Lite\luban-lite-jc

# 2. 只验证进入升级模式（不烧录）
python bsp\artinchip\tools\d213_hil.py --mode enter-upgrade `
  --upgrade-target brom --port COM9 --skip-build

python bsp\artinchip\tools\d213_hil.py --mode enter-upgrade `
  --upgrade-target bootloader --port COM9 --skip-build

# 3. OS-only 完整闭环（默认 --flash-scope os）
python bsp\artinchip\tools\d213_hil.py --mode full `
  --confirm-board d213-demo128 --skip-build --port COM9 --device 9 `
  --flasher artinchip-flash `
  --flasher-path C:\path\to\artinchip-flash\target\release\artinchip-flash.exe

# 4. 整包恢复（独立授权；重写 spl+env+os）
python bsp\artinchip\tools\d213_hil.py --mode full `
  --confirm-board d213-demo128 --confirm-full-flash `
  --flash-scope full --port COM9 --skip-build
```

`upgcmd` 后端示例（兼容/对照，只写 os 分区）：

```powershell
python bsp\artinchip\tools\d213_hil.py --mode full `
  --confirm-board d213-demo128 --flasher upgcmd --flash-scope os `
  --port COM9 --skip-build
```

状态码：`PASS`、`DRY_RUN`、`UPGRADE_ENTRY_OK`、`BUILD_FAIL`、
`PACKAGE_FAIL`、`IMAGE_MISSING`、`IMAGE_MISMATCH`、`RESOURCE_BUSY`、
`SERIAL_BUSY`、`UPGRADE_ENTRY_FAIL`、`DEVICE_NOT_FOUND`、
`DEVICE_AMBIGUOUS`、`FLASH_FAIL`、`FLASH_INTERRUPTED`、`BOOT_FAIL`、
`BOOT_TIMEOUT`、`BUILD_ID_MISMATCH`、`TEST_FAIL`、`TEST_TIMEOUT`、
`TEST_KNOWN_FAIL`、`AUTHORIZATION_REQUIRED`。

安全边界：

- 默认 `--mode plan`，不改变板子状态；
- `--mode full` 必须给出 `--confirm-board d213-demo128`；
- 默认 `--flash-scope os` 使用 `--partition os`，只写 `image.target.os`；
  整包 `--flash-scope full` 才需要 `--confirm-full-flash`；
- 烧录后必须通过 `aic_build_id` 与 manifest 的 Build ID 比对；
- `artinchip-flash` 不要加 `--verbose`：日志 I/O 会拖慢 updater
  重新枚举的时序，导致重连超时；
- 默认烧录尝试 1 次（最大 3 次），失败不自动重刷；
- 构建、串口和 USB 目标分别使用文件锁，防止并发 Agent 互相干扰；
- 不执行 `flasherase`、eFuse 或安全配置操作。

实机结果（2026-11-06/07，D213EC-DEMO-V4-0，COM9）：

- `d213_hil.py --mode full --flash-scope os`：进入升级 PASS、OS-only
  烧录 PASS（约 42 s，全部 CRC OK）、启动 PASS（约 2.1 s）、
  `aic_build_id` 与 manifest 一致、utest 仅 `bsp.d213.fpu.context`
  已知失败；
- OS-only 只发送 `image.info` + `image.target.os`；两次连续 OS-only
  前后 `spl`（4 个采样点）和 `env` 的 CRC 完全不变，`env`/`os` 与镜像
  CRC 一致；`rodata`/`data` 为 NFTL，线性 MSH 读命令不可读，明确标记
  未验证（烧录计划从不选择它们）；
- Build ID manifest：`artinchip/artifacts/d213-utest/manifest.json`
  （Build ID、Git SHA、config hash、ELF/ITB/IMG SHA256）；
- `aicupg` → `upgcmd -l` 识别为 `Boot ROM (D21xB)`；
- `aicupg gotobl` → `upgcmd -l` 识别为 `Bootloader`；
- 恢复：串口 Ctrl-C + `nand_boot` 回到 RT-Thread，无需重新上电；
- `d213_utest.py`：`uassert`、`core.irq`、`core.semaphore`、
  `core.mutex`、`core.mutex_pi` PASS；`bsp.d213.*` 仅有
  `bsp.d213.fpu.context` 已知失败（exit 2）。
- 本机 `upgcmd image` 在 updater 重新枚举阶段会 `Access denied`，
  因此 HIL 默认使用 `artinchip-flash`；Bootloader 模式的 USB 端点
  不稳定，`gotobl` 后建议 `--no-updater`。
- BROM 的 USB 偶发不枚举（Windows PnP 看不到设备、串口无输出）：
  `aicupg` 已 latch 但 30 s 内没有设备出现，HIL 以
  `UPGRADE_ENTRY_FAIL` 退出且不自动重试；此时只能断电重连。之后
  `artinchip-flash` 路径完全不调用 `upgcmd`，避免其 USB 探测把 BROM
  会话卡死。

## 支持矩阵

| 配置 | 说明 | 构建 | 打包 | 实机 |
|---|---|---|---|---|
| `d213-minimal` | UART0、tick、FinSH、FPU 自检 | PASS | PASS | PENDING |
| `d213-default` | 原默认外设 + LVDS/DE + GE + GT911 | PASS | PASS | PENDING |
| `d213-utest` | utest 固件 + WRI/aicupg + Build ID | PASS | PASS | PASS（OS-only 闭环 + utest） |
| `d126-minimal` | UART0、tick、GPIO、CLIC/CORET | PASS | PASS（占位） | PENDING |

D126 真实固件需要 `d126-demo124-nor` 的原厂 `bootloader.bin`。没有
真实 bootloader 时，`mk_firmware.py` 生成的镜像会带
`PLACEHOLDER_DO_NOT_FLASH`，不可烧录。

## 重要限制

- BUILD/STATIC/PACKAGE PASS 不等于 HW PASS。
- D213 实机验证需要 D213EC-DEMO-V4-0、UART0、原厂升级工具。
- D126 实机验证需要 D126FxR-DEMO-V1-1、真实 bootloader、确认 PSRAM
  容量/UART 引脚。
- 自动烧录默认禁用：`d213_hil.py` 仅在
  `--mode full --confirm-board d213-demo128` 时执行，且默认只写
  `image.target.os`，不碰 SPL/Bootloader/env/data；
  `--flash-scope full` 需要额外的 `--confirm-full-flash`。
- 禁止擦 eFuse、修改安全启动状态或执行 `flasherase`。
- 原厂 Bootloader/PBP/IMG 不提交到 Git。

实机步骤见 `TEST-TOMORROW.md`。
