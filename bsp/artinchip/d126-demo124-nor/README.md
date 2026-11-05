# ArtInChip D126 (D12p) RT-Thread BSP

D126FxR-DEMO-V1-1 的原生 RT-Thread BSP。当前版本完成了 D12p/E907F
最小启动、CORET tick、UART0 控制台、GPIO pin 设备、ELF 检查和固件
打包流程。显示、触摸和其余外设尚未在实机上验证。

- RT-Thread upstream baseline: `89c70abccd0359f9f7ba9df2e81dbe764d3e78d3`
- CPU port: `libcpu/risc-v/common`（Next RISC-V，PR #10320）
- 原来的 BSP `cpu/` 已删除

## 1. 目标板确认

官方 D126 页面：

<https://aicdoc.artinchip.com/topics/quickstart/d126-boards-d12xpro.html>

页面锚点包含 `d126fxv-demo-v2-0`，但页面中的
**D126FxR-DEMO-V1-1** 对应的 Luban-Lite 目标是：

```text
target/d12p/d126-demo124-nor
```

因此本 BSP 目录命名为 `d126-demo124-nor`。官方页面确认的板级能力：

| 项目 | 官方信息 | 本 BSP 状态 |
|---|---|---|
| 封装 | BGA124 | 已确认 |
| SPI NOR | 16 MB | 已按 16 MB 分区打包 |
| PSRAM | 8 / 16 MB | 默认按 8 MB 保守配置，待实物确认 |
| 显示 | MIPI DSI | 待屏幕/排线确认 |
| 触摸 | CTP | 参考同系列 GT911，待实物确认 |
| 调试串口 | UART0 | 按 PA0/PA1、115200 实现 |
| 存储 | SPI NOR + SDIO Wi-Fi + TF | 仅 NOR 打包，SDIO 未实现 |

仍需实物或原理图确认：

- 具体 D126 料号和 PCB 修订；
- 实际贴装的 PSRAM 型号与容量；
- 屏幕型号、MIPI 通道、复位/背光引脚；
- 触摸 IC、I2C 实例、复位/中断引脚；
- 晶振、PLL 实际频率和 UART0 实际波特率。

在确认之前，`pack/pbp_cfg.json` 中的 PSRAM 初始化参数、屏幕参数和
触摸参数都不能视为已验证。

## 2. 当前状态

| 项目 | 状态 | 说明 |
|---|---|---|
| Common Port | PASS | RT-Thread PR #10320 Next RISC-V |
| BUILD | PASS | macOS + `riscv64-unknown-elf-gcc 16.1.0` |
| HOST | PASS | `../tools/check_elf.py` 静态检查 |
| PACKAGE | PASS（占位 bootloader） | 生成 ITB 和最终 `.img` |
| HW | PENDING | 未在 D126 实物上启动 |
| PSRAM 边界 | PENDING | 默认只使用 8 MB |
| GPIO | BUILD PASS / HW PENDING | PA..PF、PM/PN 读写；PA..PF 中断 |
| UART0 | BUILD PASS / HW PENDING | 轮询控制台 + RT-Thread char 设备 |
| Tick | BUILD PASS / HW PENDING | CORET，1000 Hz |
| Display/Touch | 未实现 | 等待硬件确认 |
| CAN FD | 未实现 | 不能复用 D213 Classic CAN |

本 BSP 使用 RT-Thread 官方 `libcpu/risc-v/common`，原来 BSP 内的
`cpu/` 自维护 E907 port 已删除。板级差异只保留在 `board/`：

- CLIC MMIO/quirk；
- CORET `rdtime`/`mtimecmp`；
- UART0 时钟/引脚；
- GPIO v1.2 pin 驱动；
- D12p 打包。

D213 回归：同一分支上 D213 minimal/default clean build 均 PASS，
详见 `artifacts/manifest.md` 和 `bsp/artinchip/TEST-TOMORROW.md`。

## 3. 目录结构

```text
bsp/artinchip/d126-demo124-nor/
├── Kconfig
├── SConstruct
├── SConscript
├── rtconfig.py
├── .config / rtconfig.h        # 由 .config 生成
├── board/
│   ├── interrupt.c             # CLIC platform hooks
│   ├── timer.c                 # CORET rdtime/mtimecmp overrides
│   ├── clock.c                 # 最小 CMU/PLL/UART0 时钟推导
│   ├── uart.c                  # UART0 控制台和 char 设备
│   ├── board.c                 # 板级初始化
│   ├── board.h
│   └── link.lds.S              # PSRAM 链接脚本
├── configs/
│   └── d126-minimal.config     # 可复现构建配置
├── drivers/
│   └── drv_gpio.c              # GPIO v1.2 pin 设备
├── applications/
│   └── main.c                  # tick / FPU / memtest 自检
├── pack/
│   ├── image_cfg.json          # D12p 16 MB SPI NOR 分区
│   ├── pbp_cfg.json            # 原厂 PSRAM PBP 参数
│   └── env.txt
├── tools/
│   ├── check_elf.py            # shared checker wrapper
│   └── mk_firmware.py
└── docs/
```

## 4. 构建

依赖：

- Python 3 + `scons` + `kconfiglib`（仓库已有 `.venv` 时直接使用）；
- `riscv64-unknown-elf-gcc`，支持 `rv32imafc` / `ilp32f`；
- 当前验证的编译器：Homebrew `riscv64-unknown-elf-gcc 16.1.0`。

```bash
cd bsp/artinchip/d126-demo124-nor
RTT_EXEC_PATH=/opt/homebrew/bin \
  /Users/boa/Documents/dev/rt-thread/.venv/bin/scons --pyconfig-silent
RTT_EXEC_PATH=/opt/homebrew/bin \
  /Users/boa/Documents/dev/rt-thread/.venv/bin/scons -j8
python3 tools/check_elf.py --elf rtthread.elf
```

`--pyconfig-silent` 只应在修改 `.config` 后执行；`rtconfig.h` 必须由
`.config` 生成，不手工编辑。

当前构建结果（`configs/d126-minimal.config`，含 GPIO 和 common port）：

```text
text    data    bss     dec     hex
47544   744     10064   58352   e3f0
```

`rtthread.elf` 为 ELF32、RISC-V、RVC + single-float ABI，入口
`0x40000000`。`tools/check_elf.py` 会检查 common `system_vectors`、
`.percpu`、BSS/heap 边界，并拒绝旧的 `__Vectors`/`PendSV_Handler`
符号。

## 5. 打包

`tools/mk_firmware.py` 使用 Luban-Lite 的纯 Python 镜像工具，生成
`d12p_os.itb` 和 SPI NOR 整包。原厂 `bootloader.bin` 和 `d12p.pbp`
不进入本仓库，需要从 SDK 获取。

```bash
# 真实固件：必须传入为 d126-demo124-nor 构建的 bootloader.bin
AIC_SDK_DIR=/path/to/luban-lite-jc \
python3 tools/mk_firmware.py \
  --elf rtthread.elf \
  --bootloader /path/to/bootloader.bin \
  --psram-size 8m \
  --out images
```

CI/打包流程验证可以加：

```bash
python3 tools/mk_firmware.py \
  --sdk /path/to/luban-lite-jc \
  --elf rtthread.elf \
  --out images \
  --allow-placeholder-bootloader
```

**注意：** 占位 bootloader 生成的镜像不可启动，绝对不能烧录。
真实固件必须使用与 D126 板型、PSRAM 容量和 SPI NOR 配置匹配的
bootloader。

生成文件：

- `d12p_os.itb`：RT-Thread 应用 FIT 镜像；
- `d12p_d126-demo124-nor_v1.0.0.img`：SPI NOR 整包；
- `pbp_cfg.bin`：由 `pbp_cfg.json` 生成；
- `bootloader.aic`：bootloader 容器。

使用 `--allow-placeholder-bootloader` 时，脚本会把最终镜像重命名为
`*_PLACEHOLDER_DO_NOT_FLASH.img`，并写入 `DO_NOT_FLASH.txt`。

分区表（16 MB SPI NOR）：

```text
spl       512 KiB
env       128 KiB
env_r     128 KiB
userid    256 KiB
os        4 MiB
rodata    9 MiB
data      1 MiB
```

### Bootloader 地址

`mk_firmware.py` 按原厂 bootloader 内存布局计算：

```text
text_base = 0x40000000 + PSRAM_SIZE - 0xC0000 - 0x7FF00
load_base = text_base - 0x100
```

8 MB PSRAM 时为 `load=0x406c0000`、`entry=0x406c0100`。如果实际
板卡是 16 MB PSRAM，必须使用 `--psram-size 16m`，并确认 bootloader
本身也是按 16 MB 构建的。

### 手工烧录

本 BSP 不自动烧录，也不擦写 eFuse/安全状态。请使用 ArtInChip 官方
USB 升级工具或量产工具，并确认：

1. 镜像中的 bootloader 与实际板卡匹配；
2. PSRAM 容量和 PBP 配置匹配；
3. 使用的是 `spl`/`os` 正确分区，不覆盖 `userid`/安全数据。

具体工具版本和命令在实机确认后再补充为 HW 验证项。

## 6. 启动和内存布局

D12p 的厂商启动链为：

```text
BROM -> PBP (PSRAM init) -> SPL/bootloader -> d12p_os.itb -> RT-Thread
```

RT-Thread 镜像链接在 PSRAM：

```text
0x40000000  .text / .rodata / .data / .bss
            .percpu（common 早期栈；IRQ 栈在 .bss）
            system heap -> 0x40800000 (8 MB PSRAM)
```

入口流程（全部来自 common port）：

1. common `_start` 读取 `mhartid`，设置 `gp` 和 `.percpu` 早期栈；
2. `riscv_early_setup()`（BSP 可覆盖）；
3. `riscv_bss_init()` 清 BSS；
4. `riscv_boot_hart_init()`；
5. `kernel_start` 初始化 IRQ 栈，设置 `mtvec = system_vectors`；
6. `rt_hw_board_init()` 初始化 CLIC、CORET、UART0、heap；
7. 启动调度器。

CLIC 细节：

- CLIC 基址：`0x20800000`；
- common 使用 direct mode：`mtvec = system_vectors`；
- `platform_get_clic_base/size/quirks()` 提供 MMIO 和 MINTTHRESH quirk；
- CORET IRQ 7，1000 Hz；
- `rt_hw_interrupt_umask(7)` 使能 CLICINTIE[MTIP]；
- 上下文切换由 common `context_gcc.S` 直接完成，不再使用
  TSPEND/PendSV 向量。

## 7. 控制台和自检

UART0 使用 PA0/PA1、115200 8N1。启动后应看到：

```text
ArtInChip D126 (D12p) RT-Thread BSP
CPU: T-Head E907F (RV32IMafc), ... Hz
UART0: ... Hz, 115200 baud
PSRAM: 0x40000000, 8 MB
...
 \ | /
- RT -     Thread Operating System
...
msh >
```

MSH 命令：

```text
d126_selftest        # tick + FPU 上下文自检
d126_memtest 1024    # heap/PSRAM 1 MiB 图案测试
pin num PA.0         # 查看 pin 编号
pin mode PA.0 output
pin write PA.0 high
pin read PA.0
```

`d126_selftest` 会创建两个 FPU 线程并强制调度，用来发现 FPU 上下文
保存/恢复错误。`d126_memtest` 通过堆分配做图案和地址走线测试，不能
覆盖 heap 之外的 PSRAM。当前只在构建阶段验证，HW 结果待补。

## 8. 配置

关键 `.config`：

```text
CONFIG_BOARD_ARTINCHIP_D126_DEMO124_NOR=y
CONFIG_RT_USING_RISCV_NEW_COMMON=y
CONFIG_ARCH_RISCV32=y
CONFIG_ARCH_RISCV_M_MODE=y
CONFIG_CPU_BASE=0x20000000
CONFIG_AIC_PSRAM_SIZE=0x800000
CONFIG_RT_TICK_PER_SECOND=1000
CONFIG_RT_USING_DEVICE=y
CONFIG_RT_USING_PIN=y
CONFIG_BSP_USING_GPIO=y
CONFIG_BSP_UART0_BAUD=115200
```

当前未启用：

- `RT_USING_SERIAL`（控制台是自定义 char 设备）；
- 文件系统、网络、SDIO、USB；
- 显示/触摸/LVGL。

## 9. 已知限制

- 没有在 D126 实物上启动过，所有 HW 项均为 PENDING。
- PSRAM 默认只使用 8 MB；16 MB 版本需要改 `AIC_PSRAM_SIZE` 并用
  对应 bootloader 重新打包。
- `clock.c` 从 PLL 寄存器推导频率；如果 PBP 的 PLL 状态与假设不符，
  UART 波特率和 tick 频率会偏移。
- 控制台 char 设备的 `read` 是阻塞轮询；后续应替换为 UART 中断 +
  ring buffer 的 `rt_serial_device`。
- GPIO 中断只实现了 PA..PF（IRQ 68..73），PM/PN 未实现中断。
- CLIC 当前使用 common direct mode；如果实物只支持 SHV/vector
  mode，需要补充 `RISCV_CLIC_QUIRK_SHV` 和硬件 vector table。
- `clock.c` 从 PLL 寄存器推导频率；如果 PBP 的 PLL 状态与假设不符，
  UART 波特率和 tick 频率会偏移。
- 未实现 I2C、SPI/QSPI、WDT、RTC、PWM、CAN FD、MIPI DSI、触摸。
- 不使用 D213 的 DDR/PLIC/LVDS/CAN 实现。

## 10. 后续阶段

1. 实机串口日志、tick、FPU、GPIO 回环验证；
2. 确认 PSRAM 容量、PLL 频率、UART 时钟；
3. 将 UART0 改为标准 `rt_serial_device`（中断 RX）；
4. I2C0 + GT911、MIPI DSI + EK79007AD（硬件确认后）；
5. 显示 framebuffer、VSync、cache/DMA 同步、局部刷新；
6. 仅在上述验证后提取 D21x/D12p 公共 HAL/Driver；
7. 用 D133 或第二块 D12p 板验证后再考虑上游化。

## 11. 文档

```text
docs/01-board-mapping.md     板卡和原厂 target 对应关系
docs/02-architecture.md      CPU/CLIC/CORET/内存/启动设计
docs/03-build-package.md     构建、ELF 检查、打包和烧录清单
docs/04-testing.md           测试分级、命令和实机 checklist
docs/05-ip-compatibility.md  D21x/D12p IP 兼容矩阵
docs/06-display-touch.md     显示/触摸现状和计划
docs/07-upstream-roadmap.md  上游化和 D133 复用建议
docs/08-provenance.md        代码/配置来源和许可
```
