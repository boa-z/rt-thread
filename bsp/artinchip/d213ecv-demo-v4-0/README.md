# ArtInChip D213ECV-DEMO-V4-0 BSP 说明

## 1. 开发板简介

- 官方资料：<https://aicdoc.artinchip.com/topics/product/d213ecV4.html>
- 原理图：`D213EC-DEMO-V4-0.pdf`（ArtInChip 官方资料页提供）
- SoC：ArtInChip D213ECV（D21x 系列）
- CPU：T-Head C906，RV64GC（IMAFDC），M-mode
- 内存：128 MB DDR3
- 存储：128 MB SPI NAND（QSPI0）
- 调试串口：UART0，PA.0/PA.1，115200 8N1
- 固件加载地址/入口：`0x40000000`

开发板主要资源：

- 7 英寸 1024×600 LVDS 显示屏，电容触摸；
- 10.1 英寸 800×1280 MIPI 显示屏接口（本 BSP 未实现 MIPI 输出）；
- QSPI NAND、TF/SD 卡槽；
- 100M Ethernet（GMAC0，RTL8201F PHY）；
- Wi-Fi 模块接口；
- CAN0、CAN1；RS485 ×2、RS232 ×2；
- USB0（Device）、USB1（Host）；
- RTC、扬声器、数字麦克风。

软件环境：

- RT-Thread v5.4.0，Next RISC-V common port（PR #10320）；
- 上游基线 `6c955645ec82372469f20746ead53e2ce3b5eda5`；
- CPU 移植代码位于 `libcpu/risc-v/common`，本 BSP 不修改该目录；
- 默认 `.config` 使用 `demo128-nand` 软件配置（UART0/1、GPIO、I2C3、
  CAN0/1、PWM、WDT、RTC、GPAI6/KEYADC、1024×600 LVDS/DE 双
  Framebuffer、GE Normal 及测试命令）。

### 命名说明

| 名称 | 含义 |
|---|---|
| `D21x` | SoC 系列 |
| `D213ECV` | SoC 型号 |
| `D213ECV-DEMO-V4-0` | 实体开发板，对应目录 `d213ecv-demo-v4-0/` |
| `demo128-nand` | Luban-Lite 软件配置、分区布局和镜像产品名 |

`demo128-nand` 保留在原厂路径（`target/d21x/demo128-nand/`）、分区配置和
镜像文件名中，不是开发板型号。Kconfig 符号
`BOARD_ARTINCHIP_D213_DEMO128`、UART 启动横幅和 HIL 参数
`--confirm-board d213-demo128` 属于既有兼容接口，未随目录改名调整。

## 2. 外设支持

| 外设 | 支持情况 | 说明 |
|---|---|---|
| UART0 | 支持 | 控制台，轮询接收，FinSH |
| UART1 | 支持 | RS232，支持 TX DMA 和 RX ring |
| UART2/4/7 | 支持 | 多实例驱动；UART7 用于 BT HCI |
| GPIO / Pin | 支持 | 输出、输入、上下拉、双边沿中断 |
| I2C0–I2C3 | 支持 | 主机模式；I2C3 接 GT911 触摸 |
| CAN0/CAN1 | 支持 | 经典 CAN 2.0A/B，**不支持 CAN FD** |
| PWM | 支持 | 4 通道 |
| WDT | 支持 | 启动、喂狗、停止、超时复位 |
| RTC | 支持 | 秒计数和一次性闹钟 |
| TSEN | 支持 | CPU 温度通道 |
| GPAI ADC | 支持 | 通道 6，12 bit，ADCIM 校准 |
| KEYADC | 支持 | K4–K7 按键，30 ms 去抖 |
| 加密引擎 CE | 支持 | AES、MD5、SHA-1/224/256/384/512 |
| SD 卡（SDMC1） | 支持 | IDMA/FIFO 读写，FAT 挂载 |
| SDIO（SDMC2） | 部分支持 | 枚举和 CMD52 可用；CMD53 数据阶段受限 |
| Ethernet（GMAC0） | 支持 | RMII，RTL8201F，DHCP；GMAC1/RGMII 未实现 |
| USB1 Host | 支持 | EHCI/OHCI，MSC + FAT |
| USB0 Device | 支持 | CDC ACM；OTG 自动角色切换未实现 |
| QSPI NAND + MTD | 支持 | 读、写、擦除、坏块管理；声明分区默认只读 |
| NAND 文件系统 | 支持 | `/rodata` 只读 FAT，`/nand` LittleFS + 持久化 BBT |
| LVDS/DE 显示 | 支持 | 1024×600，双 Framebuffer，VSync |
| GE | 支持 | Normal 模式填充、位块传输、混合、2× 缩放、90° 旋转 |
| GT911 触摸 | 支持 | I2C3，多点坐标读取 |
| LVGL | 支持（可选） | v9.6.0，外部组件，默认关闭 |
| 音频 | 支持 | `sound0` 播放、`dmic0` 采集 |
| 视频 | 部分支持 | VE v1.0 资源/中断控制和 DE 图层；编解码外部实现 |
| MIPI/RGB 显示 | 不支持 | — |
| eMMC | 不支持 | — |
| Wi-Fi/蓝牙协议栈 | 不支持 | 仅有硬件接口 |
| AMIC/I2S 采集 | 不支持 | DMIC 已支持 |

## 3. 编译说明

### 3.1 工具链

- RISC-V GNU 工具链，提供 `riscv64-unknown-elf-*`；
- 已验证：Xuantie-900 V2.6.1（GCC 10.2.0）和 V3.3.0（GCC 14.3.0）；
- Python 3、SCons、kconfiglib（已验证 Python 3.13、SCons 4.8.1、
  kconfiglib 14.1.0）。

### 3.2 编译

```sh
cd bsp/artinchip/d213ecv-demo-v4-0
export RTT_ROOT=/path/to/rt-thread
export RTT_EXEC_PATH=/path/to/toolchain/bin
export RTT_CC=gcc
scons -j8
```

产物位于 BSP 目录：

```text
rtthread.elf
rtthread.bin
rtthread.map
rtthread.asm
link.lds
```

`rtthread.elf` 和 `rtthread.bin` 是完整的 BSP 产物，编译不依赖 Luban-Lite
SDK。生成可烧录的原厂 `.img` 才需要 SDK 镜像工具和 Bootloader 资产，
见 4.2。

ELF/map 检查：

```sh
python tools/check_elf.py
```

### 3.3 配置

`rtconfig.h` 由 `.config` 生成，不要手工修改。

```sh
scons --menuconfig
scons --pyconfig-silent
scons -j8
```

`configs/` 提供可复现配置：

| 配置 | 用途 |
|---|---|
| `d213-minimal.config` | UART0 + GPIO 最小系统 |
| `d213-default.config` | 默认外设 + LVDS/DE + GE + GT911 |
| `d213-utest.config` | 默认配置 + utest + NAND 测试 |
| `d213-nand-readonly.config` | QSPI0/SPI NAND/MTD 只读 |
| `d213-sdcard.config` | SDMC1 + FAT 自动挂载 |
| `d213-sdcard-utest.config` | SDMC1 IDMA 只读回归 |
| `d213-sdio-utest.config` | SDMC2 SDIO 回归 |
| `d213-audio-utest.config` | `sound0` + `dmic0` |
| `d213-eth-utest.config` | GMAC0 + DHCP |
| `d213-ce-utest.config` | 加密引擎 |
| `d213-usb-utest.config` | USB1 Host MSC/FAT |
| `d213-usbdev-utest.config` | USB0 Device CDC ACM |
| `d213-usbdev-only.config` | 最小固件 + USB0 Device |
| `d213-dma.config` | 外部 DMAC + `dma_test` |
| `d213-dma-utest.config` | 外部 DMAC 回归 |
| `d213-uart-dma-utest.config` | UART1 TX DMA 回归 |

使用某个配置：

```sh
cp configs/d213-nand-readonly.config .config
scons --pyconfig-silent
scons -j8
```

### 3.4 LVGL（可选）

LVGL 默认关闭，源码不随 BSP 提交。启用 `BSP_USING_LVGL` 后，需要官方
LVGL v9.6.0 和外部适配组件 `lvgl-aic-standalone`：

```sh
git clone --branch v9.6.0 --depth 1 \
    https://github.com/lvgl/lvgl.git /path/to/lvgl-9.6.0
git clone https://github.com/boa-z/lvgl-aic-standalone.git \
    /path/to/lvgl-aic-standalone
export LVGL_DIR=/path/to/lvgl-9.6.0
export LVGL_AIC_DIR=/path/to/lvgl-aic-standalone
scons --menuconfig
scons --pyconfig-silent
scons -j8
```

默认使用软件帧拷贝和 GT911 输入；GE 帧拷贝为可选项。

## 4. 使用说明

### 4.1 启动流程

本 BSP 不替换原厂启动链：

```text
BROM
  -> 原厂 SPL/Bootloader（DDR 初始化、NAND 分区）
  -> 从 os / os_r 分区加载 OS FIT
  -> rtthread.elf，入口 0x40000000
  -> M-mode common port：BSS、栈、堆、PLIC/CLINT、板级初始化
  -> RT-Thread 调度器、UART0 控制台、FinSH
```

Bootloader、DDR 参数和 PBP 是外部 Luban-Lite 资产，不由本 BSP 构建或
提交。仅升级 OS 镜像时不会改写 SPL、env 和 data 分区。

内存布局：

```text
0x40000000  .text / .rodata / .data / .bss
            中断栈
            heap
0x47800000  heap 结束
0x47B80000  原厂 Bootloader 保留区
0x48000000  128 MB DDR 结束
```

镜像必须能放入原厂 `os` 分区（demo128 布局约 4 MB）。

### 4.2 固件打包

打包使用 Luban-Lite V1.3.3 快照
`4060f85bd27a1eb555952921139b77cc41bdeb4e`
（<https://github.com/boa-z/luban-lite>）中的镜像工具和原厂
Bootloader/PBP/DDR 资产。这些资产不提交到本仓库。

SDK 必须提供：

```text
tools/scripts/mk_image.py
tools/scripts/mkenvimage.py
tools/scripts/gen_partition_table.py
tools/scripts/mk_private_resource.py
target/d21x/demo128-nand/pack/bootloader.bin
bsp/artinchip/sys/d21x/d21x.pbp
```

在仓库根目录创建 Python 虚拟环境：

```sh
uv venv .venv
source .venv/bin/activate          # Windows: .venv\Scripts\activate
uv pip install -r tools/requirements.txt
uv pip install pycryptodomex
```

回到 BSP 目录后打包：

```sh
python tools/mk_firmware.py --sdk /path/to/luban-lite \
    --elf rtthread.elf --out images
```

脚本生成 `partition.json`、`ddr_init.bin`、`d21x_os.its/.itb`、
`bootloader.aic`、`env.bin`，并组装最终镜像：

```text
d213_demo128_page_2k_block_128k_v1.0.0.img
```

`mk_firmware.py` 使用运行它的 Python 解释器。未激活虚拟环境时，可通过
`--python` 指定。脚本不会写设备。

### 4.3 烧录

使用 ArtInChip 官方 USB 升级工具或量产工具。烧录前确认：

- 镜像与 D213ECV-DEMO-V4-0 匹配；
- 如需保留原厂 Bootloader，只升级 `os` 分区；
- 供电和 UART0 连接稳定；
- 已了解恢复方式。

本仓库不提供烧录脚本，也不会自动擦写 eFuse、安全配置或 Bootloader。

### 4.4 运行结果

UART0（115200 8N1）应输出 RT-Thread banner 和板级信息：

```text
 \ | /
- RT -     Thread Operating System
 5.4.0 build ...
 2006 - 2025 Copyright by RT-Thread team
...
ArtInChip D213 demo128 RT-Thread BSP
...
msh >
```

常用 MSH 命令：

```text
aic_build_id                  # Build ID、Git SHA、配置哈希、编译时间
aic_selftest                  # 板级自检
utest_list                    # 列出测试项（utest 配置）
utest_run -thread bsp.d213.*  # 运行 D213 测试
aic_nand_info                 # SPI NAND 信息（NAND 配置）
aic_nand_partitions           # 分区表
nand_lfs_info                 # LittleFS 信息
aicupg [gotobl]               # 进入原厂升级模式（WRI 配置）
```

`bsp.d213.smoke`、`bsp.d213.uart0`、`bsp.d213.gpio` 等测试项位于 `utest/`。
默认和最小配置不链接测试用例；使用 `configs/d213-utest.config` 构建测试
固件。

## 5. 注意事项

- **CAN FD 不支持。** D21x CAN 控制器为经典 CAN 2.0A/B，不要启用原厂
  `canfd` 驱动。外部总线测试需要收发器、终端电阻和第二节点。
- **Bootloader 不在仓库内。** 原厂 Bootloader、PBP、DDR 初始化二进制由
  本地 Luban-Lite SDK 提供；仅替换 OS 镜像时不会修改 SPL/env/data。
- **NAND 声明分区只读。** BSP 不写 `env`/`os`/`os_r`，写操作仅允许在
  未分配的 `nand_scratch` 区域。`data`/`data_r` 为 NFTL，未注册为线性
  MTD 设备；NFTL、Refresh 和 OTA 更新引擎未实现。
- **SDIO CMD53 受限。** SDMC2 可完成枚举和 CMD52，CMD53 数据阶段
  不可用。
- **USB0 Device 控制台切换。** `usb_dev_console off` 当前可能挂起
  shell，不建议在无人值守场景使用。
- **显示与视频。** LVDS/DE 和 GE Normal 已实现；MIPI/RGB 输出、GE
  Draw Unit 和视频编解码未实现。
- **音频。** 播放和 DMIC 采集已实现；AMIC/I2S 采集未实现。
- **网络与无线。** GMAC0 可用；GMAC1/RGMII 和 Wi-Fi/蓝牙协议栈未实现。
- **TSEN** 仅支持 CPU 通道单次采样，GPAI 通道和告警未实现。
- **FPU 上下文。** `libcpu/risc-v/common` 未在本分支修改，线程 FPU
  上下文依赖上游修复。
- **LVGL 为外部组件。** 默认构建不依赖 LVGL，升级 LVGL 时只调整外部
  固定版本和适配组件。

## 6. 目录结构

```text
d213ecv-demo-v4-0/
├── applications/   启动入口、自检、FPU 测试、外设 MSH 命令
├── board/          启动、时钟、引脚、面板/供电描述符、分区和挂载策略
├── configs/        可复现构建配置
├── littlefs/       LittleFS 文件系统
├── lvgl/           LVGL 9.6.0 集成封装（默认关闭）
├── pack/           镜像和分区配置
├── tools/          Build ID、NAND 分区生成、固件打包、ELF 检查
├── utest/          D213 utest 用例和主机测试
├── SConstruct
├── SConscript
├── Kconfig
├── rtconfig.py
├── .config
└── rtconfig.h      由 .config 生成
```

## 7. 许可与来源

BSP 自有代码使用 Apache-2.0。`../libraries/soc/d21x/hal/` 和
`../libraries/soc/d21x/include/` 中的原厂文件来自 ArtInChip Luban-Lite
（Apache-2.0），逐字节保留原始版权声明。LVGL 为 MIT 许可的外部依赖，
不随 BSP 提交。Bootloader、PBP 和 DDR 二进制由用户本地 SDK 提供，不进入
本仓库。
