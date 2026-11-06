# 01 项目总览与快速入门

## 1.1 项目定位

本仓库在 `bsp/artinchip/d213-demo128-nand` 中提供一个**独立、标准、可维护**的 ArtInChip D213 RT-Thread BSP。它的目标不是复制 Luban-Lite，而是：

- 保留 ArtInChip 原厂 Bootloader/SPL/DDR/NAND 启动链；
- 用标准 RT-Thread v5.3.0 的 CPU Port、设备模型、FinSH 和 SCons 构建体系；
- 在必要处复用 Luban-Lite 中成熟的片上控制器 HAL；
- 把 D21x 的通用外设以标准 RT-Thread 设备接口暴露出来；
- 为后续显示、存储、网络和更多 D21x 板卡提供稳定基础。

## 1.2 支持的 SoC 与开发板

| 项目 | 当前支持 |
|---|---|
| SoC | ArtInChip D213（D21x 系列） |
| CPU | T-Head C906，RV64GC（IMAFDC），Machine Mode |
| 开发板 | ArtInChip D213EC-DEMO-V4-0（demo128-nand 配置） |
| DDR | 128 MB DDR3 |
| 启动介质 | 128 MB SPI NAND（QSPI0） |
| 控制台 | UART0，PA.0/PA.1，115200 8N1 |
| 内核 | RT-Thread v5.3.0 |
| 工具链 | Xuantie-900 elf newlib GCC V2.6.1（GCC 10.2.0） |

D211、D125、D133 或其他 D21x 板卡尚未建立 BSP。不要把 D213 的引脚、显示接口或外设数量直接套用到其他型号；新增板卡请阅读 [16-porting-guide.md](16-porting-guide.md)。

## 1.3 已实现功能

### 已经实机验证（目录重组前）

- T-Head C906 M-mode 启动、BSS 清零、栈和堆初始化；
- 机器模式 Trap、PLIC 外部中断、CLINT 机器定时器；
- UART0 控制台输出与接收中断；
- RT-Thread 线程调度、信号量、互斥量、邮箱、消息队列；
- FPU 上下文保存/恢复（`fs0`、`fs1`、`fs2`-`fs11`）；
- FinSH/MSH；
- `aic_selftest` 自检命令；
- ArtInChip FIT/ITB/IMG 打包链路。

重组后的目录尚未重新上板，因此上述能力当前标记为“HW（待重组后回归）”。

### 显示与 GE 实机验证（2026-11-04）

- LVDS 1024×600 面板点亮，硬件 Color Bar 正常；
- DE + Framebuffer + RT-Thread `lcd` 设备工作；
- 双缓冲切换：fb0 红色、fb1 绿色均正常显示；
- VSync：`10/10 received`，累计 `vsync=4692`、`underflow=0`；
- GE Normal FillRect、BitBlt 实机 PASS。

GE 缩放、Alpha 混合、旋转的测试用例已修正，尚待下一次实机复测。

### 已编写源码并通过编译/静态检查（待上板）

- 标准 GPIO/Pin 设备，含输入、输出、上下拉、引脚中断与去抖；
- UART0/1/2/4 多实例（UART3/5/6/7 可编译，但 demo128 未定义板级引脚）；
- I2C0-I2C3 主机，标准 RT-Thread I2C 总线设备；
- CAN0/CAN1 经典 CAN 2.0A/B，标准 RT-Thread CAN 设备；
- PWM 4 通道；
- 看门狗；
- RTC（秒计数）；
- GE Normal 模式缩放、Alpha 混合、旋转（FillRect/BitBlt 已实机验证）；
- GT911 电容触摸（多点 DOWN/MOVE/UP 已实机验证，方向待校准）；
- LVGL 9.6.0 显示与 GT911 输入（软件渲染 + 可选 GE 帧拷贝，使用外部固定版本源码）；
- SPI NAND 只读：QSPI0 + 标准 MTD NAND + 只读分区/FIT 诊断
  （BUILD + STATIC + HOST PASS，HW PENDING）；
- 外设/显示/GE/触摸 MSH 测试命令。

### 尚未实现

- SPI NAND 写入/擦除/坏块标记、NFTL、DFS/FatFs/LittleFS、OTA；
- MIPI DSI / RGB 显示；
- USB Host/Device、SDMC/SDIO、Ethernet、Wi-Fi/蓝牙；
- 音频、视频、DVP、PSADC、EPWM、CAP/QEP、CIR、加密引擎等。

## 1.4 目录结构

```text
bsp/artinchip/
├── README.md                       # ArtInChip BSP 家族说明
└── d213-demo128-nand/
    ├── .config                     # 唯一默认配置（外设 + 显示 + GE）
    ├── Kconfig                     # BSP Kconfig 根
    ├── SConstruct                  # SCons 构建入口
    ├── rtconfig.py                 # 工具链与编译选项
    ├── rtconfig.h                  # 由 .config 生成，不手工修改
    ├── applications/
    │   ├── main.c                  # 启动自检
    │   ├── fpu_test_gcc.S          # FPU 上下文测试
    │   ├── periph_test.c           # 外设 MSH 验证命令
    │   └── touch_test.c            # GT911 触摸 MSH 验证命令
    ├── board/
    │   ├── board.h / board.c       # 板级初始化
    │   ├── clock.c                 # 外设时钟门控
    │   ├── pinmux.c                # UART 引脚复用
    │   ├── interrupt.c             # PLIC platform hooks
    │   ├── timer.c                 # CLINT accessors
    │   ├── cache.c                 # C906 range cache ops
    │   └── link.lds.S              # common-port linker script
    ├── drivers/
    │   ├── drv_uart.c              # 多 UART
    │   ├── drv_gpio.c              # GPIO/Pin
    │   ├── drv_i2c.c               # I2C
    │   ├── drv_can.c               # CAN 2.0
    │   ├── drv_pwm.c               # PWM
    │   ├── drv_wdt.c               # 看门狗
    │   ├── drv_rtc.c               # RTC
    │   └── drv_touch_gt911.c       # GT911 电容触摸
    ├── vendor/
    │   ├── include/                # 兼容头 + 原厂公共头
    │   ├── hal/                    # 原厂 HAL（逐字节保持）
    │   └── port/aic_hal_clk.c      # 精简时钟/复位适配
    ├── lvgl/                       # 外部 lvgl-aic-standalone 集成入口
    ├── docs/                       # 本文档体系
    ├── pack/                       # 分区/镜像打包配置
    └── tools/mk_firmware.py        # FIT/ITB/IMG 打包脚本
```

## 1.5 工具链与环境

已验证组合：

- Xuantie-900 elf newlib GCC V2.6.1（`riscv64-unknown-elf-gcc` 10.2.0）；
- Python 3.13 + SCons 4.8.1 + kconfiglib 14.1.0；
- Windows PowerShell 与 Linux/macOS 的 Bash 均可使用。

RT-Thread v5.3.0 的旧文档常写 Python 3.8 + SCons 3.1.2。当前仓库在 Python 3.13 + SCons 4.8.1 下已验证；如果使用 SCons 3.x，请确认其 Python 版本兼容性。

## 1.6 从零构建

### 1.6.1 获取代码

```sh
git clone https://github.com/boa-z/rt-thread.git
cd rt-thread
git checkout bsp/artinchip-d21x
```

### 1.6.2 设置环境

Bash：

```sh
export RTT_ROOT=$PWD
export RTT_EXEC_PATH=/path/to/xuantie/bin
export RTT_CC=gcc
```

PowerShell：

```powershell
$env:RTT_ROOT = (Get-Location).Path
$env:RTT_EXEC_PATH = "C:\path\to\toolchain\bin"
$env:RTT_CC = "gcc"
```

### 1.6.3 编译默认配置

```sh
cd bsp/artinchip/d213-demo128-nand
scons -j8
```

输出：

- `rtthread.elf`：可执行 ELF；
- `rtthread.bin`：裸二进制；
- `rtthread.map`：链接映射；
- `link.lds`：正常构建时由 `board/link.lds.S` 预生成；配置/clean 命令跳过。

默认 `.config` 按 D213EC-DEMO-V4-0 板级组合启用 UART0/1、GPIO、I2C3、CAN0/1、PWM、WDT、RTC、LVDS/DE Framebuffer、双缓冲、GE Normal 和外设/显示/GE MSH 测试命令。未使用的 UART2/4、I2C0/1/2 等实例保留在 Kconfig 中，可按需启用。

### 1.6.4 启用外设

```sh
scons --menuconfig
scons --pyconfig-silent
scons -j8
```

> `--useconfig=FILE` 只把文件转换为 `rtconfig.h`，不会重新计算 `select` 依赖。修改配置后应执行 `scons --pyconfig-silent`，确保 `RT_USING_*` 框架符号也被正确解析。

### 1.6.5 生成固件

在已激活的 uv 虚拟环境中执行（需要 `pycryptodomex`）：

```sh
export AIC_SDK_DIR=/path/to/luban-lite-jc
python tools/mk_firmware.py --sdk "$AIC_SDK_DIR" \
    --elf rtthread.elf --out images
```

Windows PowerShell 将 `export` 换成
`$env:AIC_SDK_DIR = "...\luban-lite-jc"` 即可。完整依赖、macOS/Linux/Windows
命令和测试见 [15-build-pack-deploy.md](15-build-pack-deploy.md)。

最终镜像：

```text
images/d21x_d213_demo128_page_2k_block_128k_v1.0.0.img
```

打包不会写设备，也不会修改 Bootloader 或 NAND 分区表。

## 1.7 串口与调试入口

- 串口参数：115200、8 数据位、无校验、1 停止位、无流控；
- 上电后应先看到 ArtInChip Bootloader 日志，再看到 `RT-Thread 5.3.0`；
- 进入 MSH 后可执行（外设命令需要先在 menuconfig 中启用对应外设）：
  - `aic_selftest`：线程、IPC、FPU 自检；
  - `periph_status`：列出已注册的 BSP 设备；
  - `i2c_scan i2c3`：扫描 I2C3；
  - `can_baud can0 500000`：设置经典 CAN 位速率；
  - `can_mode can0 normal`：设置 normal/listen/loopback；
  - `can_send can0 0x123 11 22 33`：发送标准帧；
  - `can_dump can0 5`：接收最多 5 帧；
  - `pwm_test 0 1000000 500000 3`：PWM0 输出 1 kHz、50% 占空比 3 秒；
  - `gpio_toggle PC.7 5 200`：翻转 PC.7；
  - `rtc_test`：读取 RTC；
  - `wdt_test feed|start|stop`：喂狗/启动/停止；
  - `disp_colorbar 1|0`：打开/关闭 DE 硬件彩条；
  - `disp_info` / `disp_test`：显示参数、双缓冲、VSync；
  - `ge_test`：GE FillRect/BitBlt/缩放/混合/旋转；
  - `lvgl_demo`：启动 LVGL 9.6.0 显示/点击/拖动演示（需要外部 LVGL 源码）；
  - `lvgl_touch`：统计 LVGL 触摸坐标范围，验证方向。

## 1.8 重要限制

- 当前环境没有串口设备，GPIO/I2C/CAN/PWM/WDT/RTC 的实机验证尚未完成。
- CAN 需要外部收发器和总线终端；仅编译通过不代表总线通信通过。
- RTC 闹钟、显示、存储、USB、网络、音频、视频尚未接入。
- `vendor/hal` 中的原厂文件保持原样，但依赖由本仓库的精简兼容层提供；升级 HAL 时必须重新核对依赖。
- 不要手工修改 `rtconfig.h`，也不要为了让某个驱动“先跑起来”而关闭 Cache 或修改 Bootloader 分区。

## 1.9 相关文档

- 首次移植新外设：[16-porting-guide.md](16-porting-guide.md)
- 构建/打包失败：[15-build-pack-deploy.md](15-build-pack-deploy.md)
- 外设不工作：[17-testing-troubleshooting.md](17-testing-troubleshooting.md)
- 原厂代码来源和许可证：[20-vendor-source-license.md](20-vendor-source-license.md)
