# ArtInChip D126FxR-DEMO-V1-1 BSP 说明

## 1. 开发板简介

- 官方资料：<https://aicdoc.artinchip.com/topics/quickstart/d126-boards-d12xpro.html#d126fxv-demo-v2-0>
- SoC：ArtInChip D126（D12p 系列）
- CPU：T-Head E907F，RV32IMAfc，M-mode
- 封装：BGA124
- 内存：8 MB / 16 MB PSRAM（BSP 默认按 8 MB 配置）
- 存储：16 MB SPI NOR
- 调试串口：UART0，PA0/PA1，115200 8N1
- 固件加载地址/入口：`0x40000000`

官方页面锚点为 `d126fxv-demo-v2-0`，页面中的 **D126FxR-DEMO-V1-1** 对应
Luban-Lite 目标：

```text
target/d12p/d126-demo124-nor
```

因此 BSP 目录按开发板型号命名为 `d126fxr-demo-v1-1`；`d126-demo124-nor`
保留为 Luban-Lite 目标、分区配置和镜像产品名。

开发板主要资源：

- MIPI DSI 显示屏接口和电容触摸；
- SPI NOR、SDIO Wi-Fi、TF 卡槽；
- RS485、CAN、UART；
- 摄像头和音频接口。

软件环境：

- RT-Thread v5.4.0，Next RISC-V common port（PR #10320）；
- 上游基线 `6c955645ec82372469f20746ead53e2ce3b5eda5`；
- CPU 移植代码位于 `libcpu/risc-v/common`，原 BSP 内的 `cpu/` 已删除；
- 板级差异保留在 `board/`：CLIC MMIO/quirk、CORET `rdtime`/`mtimecmp`、
  UART0 时钟和引脚、GPIO v1.2 pin 驱动、D12p 打包。

## 2. 外设支持

| 外设 | 支持情况 | 说明 |
|---|---|---|
| UART0 | 支持 | 标准 `rt_serial_device`，中断接收，轮询发送 |
| GPIO | 支持 | PA–PF 支持输出、输入、上下拉和双边沿中断；PM/PN 仅读写 |
| Tick / 调度 / IPC | 支持 | CORET 1000 Hz，semaphore、mutex、mailbox |
| 内存 | 支持 | PSRAM heap，`d126_memtest` 自检 |
| 复位 / 升级 | 支持 | `reboot`、`aicupg` 写 WRI 原因并 WDT 复位 |
| Build ID | 支持 | `aic_build_id` |
| 显示 / 触摸 | 未实现 | 等待屏幕和触摸硬件确认 |
| I2C / SPI / QSPI | 未实现 | — |
| WDT / RTC / PWM | 未实现 | WDT 仅用于 `reboot`/`aicupg` |
| CAN | 未实现 | 不能复用 D213 的经典 CAN 驱动 |
| SDIO Wi-Fi | 未实现 | — |
| FPU 线程上下文 | 已知问题 | common port 线程切换未保存 FPU 寄存器 |

## 3. 编译说明

### 3.1 工具链

- RISC-V GNU 工具链，支持 `rv32imafc` / `ilp32f`，提供
  `riscv64-unknown-elf-*`；
- Python 3、SCons、kconfiglib。

### 3.2 编译

```sh
cd bsp/artinchip/d126fxr-demo-v1-1
export RTT_ROOT=/path/to/rt-thread
export RTT_EXEC_PATH=/path/to/toolchain/bin
scons --pyconfig-silent
scons -j8
python3 tools/check_elf.py --elf rtthread.elf
```

`--pyconfig-silent` 只在修改 `.config` 后执行。`rtconfig.h` 必须由
`.config` 生成，不要手工编辑。

`configs/` 提供可复现配置：

| 配置 | 用途 |
|---|---|
| `d126-minimal.config` | UART0、GPIO、common port 最小系统 |
| `d126-utest.config` | 增加 utest 和 D126 测试 |

`rtthread.elf` 为 ELF32、RISC-V、RVC + single-float ABI，入口
`0x40000000`。`tools/check_elf.py` 检查 common `system_vectors`、
`.percpu`、BSS/heap 边界，并拒绝旧的 `__Vectors`/`PendSV_Handler` 符号。

## 4. 使用说明

### 4.1 启动流程

```text
BROM -> PBP（PSRAM 初始化）-> SPL/Bootloader -> d12p_os.itb -> RT-Thread
```

RT-Thread 镜像链接在 PSRAM：

```text
0x40000000  .text / .rodata / .data / .bss
            .percpu（common 早期栈；IRQ 栈在 .bss）
            system heap -> 0x40800000（8 MB PSRAM）
```

启动顺序：

1. common `_start` 读取 `mhartid`，设置 `gp` 和 `.percpu` 早期栈；
2. `riscv_early_setup()` 清除 E907 `MEXSTATUS` 的
   `SPUSHEN`/`SPSWAPEN`，避免硬件 IRQ 栈切换与 common port 的软件
   `mscratch` 切换叠加；
3. `riscv_bss_init()`、`riscv_boot_hart_init()`；
4. `kernel_start` 初始化 IRQ 栈，设置 `mtvec = system_vectors`；
5. `rt_hw_board_init()` 初始化 CLIC、CORET、UART0 和 heap；
6. 启动调度器。

CLIC 基址 `0x20800000`，使用 common direct mode；CORET IRQ 7，1000 Hz。

### 4.2 固件打包

`tools/mk_firmware.py` 使用 Luban-Lite 的纯 Python 镜像工具，生成
`d12p_os.itb` 和 SPI NOR 整包。原厂 `bootloader.bin` 和 `d12p.pbp` 不进入
本仓库，Bootloader 必须由同一 Luban-Lite 树为 `d126-demo124-nor` 构建。

Bootloader 查找顺序：

1. `--bootloader` 显式路径；
2. `<sdk>/target/d12p/d126-demo124-nor/pack/bootloader.bin`；
3. `<sdk>/output/d12p_d126-demo124-nor_*/images/bootloader.bin`（多个时取最新）。

```sh
python3 tools/mk_firmware.py \
  --sdk /path/to/luban-lite \
  --elf rtthread.elf \
  --psram-size 8m \
  --out images
```

仅在验证打包流程、没有真实 Bootloader 时使用
`--allow-placeholder-bootloader`。该模式生成的镜像带有
`PLACEHOLDER_DO_NOT_FLASH` 标记和 `DO_NOT_FLASH.txt`，**不可烧录**。

输出文件：

- `d12p_os.itb`：RT-Thread 应用 FIT 镜像；
- `d12p_d126-demo124-nor_v1.0.0.img`：SPI NOR 整包；
- `pbp_cfg.bin`：PBP 配置（PDAT 容器）；
- `bootloader.aic`：Bootloader 容器。

16 MB SPI NOR 分区：

```text
spl       512 KiB
env       128 KiB
env_r     128 KiB
userid    256 KiB
os          4 MiB
rodata      9 MiB
data        1 MiB
```

Bootloader 地址按原厂内存布局计算：

```text
text_base = 0x40000000 + PSRAM_SIZE - 0xC0000 - 0x7FF00
load_base = text_base - 0x100
```

8 MB PSRAM 时为 `load=0x406c0000`、`entry=0x406c0100`。16 MB PSRAM
变体必须使用 `--psram-size 16m`，并确认 Bootloader 也按 16 MB 构建。

### 4.3 烧录

使用 ArtInChip 官方 USB 升级工具或量产工具，烧录前确认：

1. 镜像中的 Bootloader 与实际板卡匹配；
2. PSRAM 容量和 PBP 配置匹配；
3. 写入 `spl`/`os` 等正确分区，不覆盖 `userid` 或安全数据。

本 BSP 不自动烧录，也不擦写 eFuse 或安全状态。

### 4.4 运行结果

UART0（115200 8N1）应输出：

```text
ArtInChip D126 (D12p) RT-Thread BSP
CPU: T-Head E907F (RV32IMafc), ... Hz
UART0: ... Hz, 115200 baud
PSRAM: 0x40000000, 8 MB
CLIC: ... IRQs, ... control bits
...
 \ | /
- RT -     Thread Operating System
...
msh >
```

常用 MSH 命令：

```text
d126_info            # 板型、CPU、PSRAM、tick 频率和 uptime
d126_tick 100        # CORET tick 精度
d126_sched 1000      # 双线程 + mutex 调度/计数
d126_ipc             # semaphore + mailbox
d126_memtest 1024    # heap/PSRAM 图案测试
aic_build_id         # Build ID / Git SHA / 配置哈希
reboot               # WRI 原因 + WDT 热复位
aicupg               # 进入原厂升级模式
pin num PA.0
pin mode PA.0 output
pin write PA.0 high
pin read PA.0
```

`d126_selftest` 保留用于 FPU 专项调查，不随启动自动运行。

串口回归工具：

```sh
python ../tools/d126_hil.py --mode serial --port COM10 --repeat 3
python ../tools/d126_utest.py --port COM10
```

## 5. 注意事项

- PSRAM 默认只使用 8 MB。原厂 Bootloader 按 8 MB 基址 `0x406c0000`
  加载；16 MB 变体需要修改 `AIC_PSRAM_SIZE` 并使用对应的 Bootloader。
- `clock.c` 从 PLL 寄存器推导频率。如果 PBP 的 PLL 状态与假设不符，
  UART 波特率和 tick 频率会偏移。
- UART0 只实现接收中断，发送为轮询；多 UART 实例、RS485 和流控未实现。
- GPIO 中断只覆盖 PA–PF（IRQ 68–73），PM/PN 不支持中断。输出模式保留
  输入缓冲（`DIR = IE|OE`），因此 `rt_pin_read` 可以回读实际引脚电平。
- CLIC 使用 common direct mode。如果实物只支持 SHV/vector mode，需要
  补充 `RISCV_CLIC_QUIRK_SHV` 和硬件 vector table。
- 线程 FPU 上下文未保存 `f0`–`f31`/`fcsr`，`d126_selftest` 中的 FPU
  用例不作为启动验收项。
- 显示、触摸、I2C、SPI/QSPI、WDT、RTC、PWM、CAN、MIPI DSI 和 SDIO
  Wi-Fi 均未实现。
- Bootloader 和 PBP 依赖外部 Luban-Lite SDK，不提交到 Git。
- Kconfig 符号 `BOARD_ARTINCHIP_D126_DEMO124_NOR`、`d126_info` 的 board
  字段和 `--confirm-board d126-demo124-nor` 保留为既有兼容接口，不代表
  当前 BSP 目录名。

## 6. 目录结构

```text
d126fxr-demo-v1-1/
├── applications/   main、自检命令、Build ID
├── board/          CLIC、CORET、时钟、UART0、板级初始化和链接脚本
├── configs/        d126-minimal、d126-utest 配置
├── drivers/        GPIO v1.2 pin 设备
├── pack/           分区、PBP 和 env 配置
├── tools/          ELF 检查和固件打包
├── utest/          D126 测试用例
├── SConstruct
├── SConscript
├── Kconfig
├── rtconfig.py
├── .config
└── rtconfig.h      由 .config 生成
```

## 7. 许可与来源

BSP 自有代码使用 Apache-2.0。RT-Thread 内核和组件遵循仓库根目录的
`LICENSE`。原厂 Bootloader、PBP 和镜像工具来自 Luban-Lite，由用户本地
SDK 提供，不进入本仓库。
