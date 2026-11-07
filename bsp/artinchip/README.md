# ArtInChip RT-Thread BSPs

本目录包含两个基于 RT-Thread **Next RISC-V common port**（PR #10320）
的 ArtInChip BSP：

```text
bsp/artinchip/
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
7. 把 ELF/BIN/IMG 和日志放到 `artifacts/`；
8. 生成 `artifacts/manifest.md`；
9. 恢复原 `.config`/`rtconfig.h`。

`artifacts/` 被 `.gitignore` 排除，不进入源码历史。

## D213 HIL 自动化（构建 → 烧录 → utest）

`tools/d213_hil.py` 复用 `build-check.py`、`mk_firmware.py` 和
`d213_utest.py`，把构建、静态检查、打包、进入升级模式、AiBurn/upgcmd
烧录、RT-Thread 启动等待和 utest 串成一条流水线，输出 `report.json`
（含 Git commit、镜像 SHA256、串口日志和每个阶段的状态）。

依赖：

- Python 3.8+、`pyserial`；
- Luban-Lite SDK 和 RISC-V 工具链（构建/打包）；
- `pycryptodomex`（SDK 的 `mk_image.py` 需要）；
- AiBurn v2.1.0 的 `upgcmd.exe`（默认
  `C:\Program Files\AiBurn\upgcmd.exe`）。

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

# 3. 完整闭环（首次自动烧录需要显式确认；默认只写 os 分区）
python bsp\artinchip\tools\d213_hil.py --mode full `
  --confirm-board d213-demo128 --port COM9 `
  --sdk C:\Project\D50T-2-Lite\luban-lite-jc
```

状态码：`PASS`、`DRY_RUN`、`UPGRADE_ENTRY_OK`、`BUILD_FAIL`、
`IMAGE_MISSING`、`IMAGE_MISMATCH`、`SERIAL_BUSY`、
`UPGRADE_ENTRY_FAIL`、`DEVICE_NOT_FOUND`、`FLASH_FAIL`、`BOOT_FAIL`、
`BOOT_TIMEOUT`、`TEST_FAIL`、`TEST_TIMEOUT`、`TEST_KNOWN_FAIL`、
`AUTHORIZATION_REQUIRED`。

安全边界：

- 默认 `--mode plan`，不改变板子状态；
- `--mode full` 必须给出 `--confirm-board d213-demo128`；
- 默认 `--flash-scope os`，只写 `image.target.os`，保留 SPL、Bootloader、
  env 和 data 分区；`--flash-scope full` 需要 `--confirm-full-flash`；
- 烧录重试上限 3 次；utest 失败不会触发自动重刷；
- 不执行 `flasherase`、eFuse 或安全配置操作。

实机结果（2026-11-06，D213EC-DEMO-V4-0，COM9）：

- `aicupg` → `upgcmd -l` 识别为 `Boot ROM (D21xB)`；
- `aicupg gotobl` → `upgcmd -l` 识别为 `Bootloader`；
- 恢复：串口 Ctrl-C + `nand_boot` 回到 RT-Thread，无需重新上电；
- `d213_utest.py`：`uassert`、`core.irq`、`core.semaphore`、
  `core.mutex`、`core.mutex_pi` PASS；`bsp.d213.*` 仅有
  `bsp.d213.fpu.context` 已知失败（exit 2）。

## 支持矩阵

| 配置 | 说明 | 构建 | 打包 | 实机 |
|---|---|---|---|---|
| `d213-minimal` | UART0、tick、FinSH、FPU 自检 | PASS | PASS | PENDING |
| `d213-default` | 原默认外设 + LVDS/DE + GE + GT911 | PASS | PASS | PENDING |
| `d213-utest` | utest 固件 + WRI/aicupg | PASS | PASS | PASS（升级模式 + utest；自动烧录待授权） |
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
