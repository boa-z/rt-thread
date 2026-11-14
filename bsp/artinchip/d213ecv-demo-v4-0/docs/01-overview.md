# 01 项目总览与快速入门

## 1.1 项目定位

本仓库在 `bsp/artinchip/d213ecv-demo-v4-0` 中提供一个**独立、标准、可维护**的 ArtInChip D213 RT-Thread BSP。它的目标不是复制 Luban-Lite，而是：

- 保留 ArtInChip 原厂 Bootloader/SPL/DDR/NAND 启动链；
- 用标准 RT-Thread v5.4.0 的 CPU Port、设备模型、FinSH 和 SCons 构建体系；
- 在必要处复用 Luban-Lite 中成熟的片上控制器 HAL；
- 把 D21x 的通用外设以标准 RT-Thread 设备接口暴露出来；
- 为后续显示、存储、网络和更多 D21x 板卡提供稳定基础。

## 1.2 支持的 SoC 与开发板

| 项目 | 当前支持 |
|---|---|
| SoC 系列 | ArtInChip D21x |
| SoC 型号 | ArtInChip D213ECV（D213 家族） |
| CPU | T-Head C906，RV64GC（IMAFDC），Machine Mode |
| 开发板 | ArtInChip D213ECV-DEMO-V4-0 |
| 原厂软件配置 | `demo128-nand`（Luban-Lite target/分区/镜像产品名） |
| DDR | 128 MB DDR3 |
| 启动介质 | 128 MB SPI NAND（QSPI0） |
| 控制台 | UART0，PA.0/PA.1，115200 8N1 |
| 内核 | RT-Thread v5.4.0 |
| 工具链 | Xuantie-900 elf newlib GCC V2.6.1（GCC 10.2.0）；V3.3.0（GCC 14.3.0）已验证 |

**命名边界：** `D213ECV` 是 SoC 型号，`D21x` 是系列，`D213ECV-DEMO-V4-0`
是实体开发板，`demo128-nand` 是原厂软件配置。目录名
`d213ecv-demo-v4-0` 按实体板卡命名；原厂配置名保留在
`target/d21x/demo128-nand/...`、镜像产品名和分区文档中，不应互相替换。

D211、D125、D133 或其他 D21x 板卡尚未建立 BSP。不要把 D213 的引脚、显示接口或外设数量直接套用到其他型号；新增板卡请阅读 [16-porting-guide.md](16-porting-guide.md)。

## 1.3 已实现功能

### 内核与基础启动（已实机回归）

- T-Head C906 M-mode 启动、BSS 清零、栈和堆初始化；
- 机器模式 Trap、PLIC 外部中断、CLINT 机器定时器；
- UART0 控制台输出与接收（RX 为 1 ms 轮询）；
- RT-Thread 线程调度、信号量、互斥量、邮箱、消息队列；
- FPU 使能（`aic_cpu_init`）；线程切换的 FPU 上下文已采用官方修复，待实机复测（见 22.6）；
- FinSH/MSH；
- `aic_selftest` 自检命令；
- ArtInChip FIT/ITB/IMG 打包链路。

目录重组后已通过 HIL 全流程回归（如 HIL `20261108-092325-14520`），上述内核/基础能力为 HW PASS；FPU 历史失败已由官方修复，当前基线待实机复测（见 22.6）。

### 显示与 GE 实机验证（2026-11-04）

- LVDS 1024×600 面板点亮，硬件 Color Bar 正常；
- DE + Framebuffer + RT-Thread `lcd` 设备工作；
- 双缓冲切换：fb0 红色、fb1 绿色均正常显示；
- VSync：`10/10 received`，累计 `vsync=4692`、`underflow=0`；
- GE Normal FillRect、BitBlt 实机 PASS；
- GE 缩放、Alpha 混合、旋转于 2026-11-09 复测：混合像素 PASS，
  2× 缩放四象限与 90° 顺时针旋转像素 PASS（`ge_test` 20 轮，
  报告 `20261109-063037`）。

### 其余已实现功能与验证状态

- 标准 GPIO/Pin 设备，含输入、输出、上下拉、引脚中断与去抖
  （`bsp.d213.gpio` API 用例 HW PASS；外部负载/波形、引脚中断与去抖未验证）；
- UART0/1/2/4 多实例（UART3/5/6 可编译，但 demo128 未引出；
  UART7 见下方 BT HCI 条目；UART1 TX DMA HW PASS，UART2/4 BUILD/STATIC）；
- I2C0-I2C3 主机，标准 RT-Thread I2C 总线设备
  （I2C3/GT911 HW PASS；I2C0/1/2 BUILD/STATIC）；
- CAN0/CAN1 经典 CAN 2.0A/B，标准 RT-Thread CAN 设备
  （控制器配置 HW PASS；外部总线仍 SKIP）；
- PWM 4 通道（`bsp.d213.pwm` HW PASS；示波器波形未验证）；
- 看门狗（`start`/`stop`/`feed`/`GET_TIMELEFT` 均实机验证；
  窗口外喂狗被硬件忽略，不喂狗时 SPL 记录 Watchdog-Reset，
  回归报告 `20261109-055217`）；
- RTC（秒计数、一次性/周期闹钟 + 中断；HW PASS，
  HIL `20261108-085811-23408`）；
- GE Normal 模式缩放、Alpha 混合、旋转（Fill/BitBlt/混合/2× 缩放/
  90° 顺时针旋转均像素校验通过，20 轮回归 `20261109-063037`；
  任意角度/翻转/其他缩放比例未验证）；
- GT911 电容触摸（多点 DOWN/MOVE/UP 已实机验证，方向待校准）；
- LVGL 9.6.0 显示与 GT911 输入（软件渲染 + 可选 GE 帧拷贝，使用外部固定版本源码；
  旧 BSP 内端口已实机验证，新 `lvgl-aic-standalone` 组件待实机）；
- SPI NAND：QSPI0 + 标准 MTD NAND，读取与 Program/Erase/Move/Mark-Bad
  均已实现；读取 HW PASS，写/擦/移动/标坏在 `nand_scratch` 上 HW PASS
  （HIL `20261108-230606-16252`）；声明分区默认只读，`os`/`os_r` FIT
  与只读 A/B 诊断 HW PASS（`bsp.d213.ota`，HIL `20261109-015923-59712`），
  分区 MSH 输出、BBT 双槽切换与看门狗复位注入已 HW PASS
  （`d213_storage_soak`，报告 `20261109-035532`；冷启动仍待测）；
  `/rodata` 只读 FAT 与 `/nand` LittleFS + 持久化 BBT 已 HW PASS
  （HIL `20261109-002129-38512`）；无 NFTL、Refresh、OTA 更新引擎；
- SDMC1 SD 卡（块设备/只读 utest/FAT/IDMA）与 SDMC2 SDIO 主机
  （RTL8733BS 枚举、CMD52 已 HW PASS，CMD53 见 12.15）；
- UART7 BT HCI 端口与 RTS/CTS 自动流控（内部回环 utest HW PASS，
  HIL `20261108-064913-21800`；RTS/CTS 未经模组对端验证）；
- 音频播放：标准 `sound0`，DSPK/SPK1 + PA_SHDN，循环 DMA
  （HW PASS；声学输出待人耳/仪器确认）；
- DMIC 采集：标准 `dmic0`，1/2 声道 record pipe
  （HW PASS，HIL `20261108-222242-3036`；共享层重构后复测
  `20261109-093546-7576`）；
- USB1 Host：CherryUSB EHCI/OHCI + MSC，U 盘块设备与 FAT 自动挂载
  （HW PASS，HIL `20261108-074359-19904`；USB0 Host 未回归，
  USB0 Device 见下条）；
- USB0 Device：CherryUSB AIC UDC + CDC ACM，字符设备 `usb-acm0`
  （枚举、`configured=1`、CDC 双向 MSH 和 `usb_dev_console on/off`
  切换均 HW PASS，on/off 连续 16 轮，回归报告 `20261109-053348`）；
- 以太网：GMAC0 RMII + RTL8201F，netdev/lwIP 2.1.2 + DHCP
  （HW PASS，DHCP 租约 + ping 网关，HIL `20261108-085007-17888`）；
- 片内温度传感器：TSEN + ADCIM 模拟前端 + SID eFuse 校准，标准
  RT-Thread sensor 设备 `temp_tsen_cpu`
  （HW PASS，HIL `20261108-092325-14520`）；
- 通用 ADC：D21x GPAI + ADCIM 校准，标准 RT-Thread ADC 设备 `gpai`，
  12 位，参考电压由 `RT_ADC_CMD_GET_VREF` 上报；当前启用 GPAI6/PA.6
  （BUILD/STATIC + utest，实机采样待验证）；
- 板载按键：GPAI6 轮询 K4 UP / K5 DOWN / K6 LEFT / K7 RIGHT 梯形电阻，
  30 ms 消抖，上报按下/松开/长按事件，MSH `keyadc`
  （BUILD/STATIC + utest，实机按键待验证）；
- 加密引擎（CE）：标准 RT-Thread hwcrypto 设备，AES-128/192/256
  ECB/CBC、MD5、SHA-1/SHA-224/256/384/512
  （HW PASS，`bsp.d213.ce` 5/5 单元，HIL `20261109-034918-80696`）；
- 外设/显示/GE/触摸 MSH 测试命令。

### 尚未实现

- SPI NAND 的 NFTL、Refresh、OTA 更新引擎；`data`/`data_r` 仍为 NFTL；
- MIPI DSI / RGB 显示；
- USB OTG 自动角色切换、Wi-Fi/蓝牙协议栈、SDIO Wi-Fi 驱动；
- 音频采集 AMIC、I2S、视频、DVP、PSADC、EPWM、CAP/QEP、
  CIR、eFuse/SID 用户 API、电源管理等。

## 1.4 目录结构

```text
bsp/artinchip/
├── README.md                       # ArtInChip BSP 家族说明
└── d213ecv-demo-v4-0/
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
    ├── ../libraries/               # 共享层：I2C 适配、D21x 设备适配/HAL
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

RT-Thread 旧文档常写 Python 3.8 + SCons 3.1.2。当前仓库在 Python 3.13 + SCons 4.8.1 下已验证；如果使用 SCons 3.x，请确认其 Python 版本兼容性。

## 1.6 从零构建

### 1.6.1 获取代码

```sh
git clone https://github.com/boa-z/rt-thread.git
cd rt-thread
git checkout bsp/artinchip-d213-dev
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
cd bsp/artinchip/d213ecv-demo-v4-0
scons -j8
```

输出：

- `rtthread.elf`：可执行 ELF；
- `rtthread.bin`：裸二进制；
- `rtthread.map`：链接映射；
- `link.lds`：正常构建时由 `board/link.lds.S` 预生成；配置/clean 命令跳过。

默认 `.config` 按 D213ECV-DEMO-V4-0 板级组合启用 UART0/1、GPIO、I2C3、CAN0/1、PWM、WDT、RTC、GPAI6/KEYADC、LVDS/DE Framebuffer、双缓冲、GE Normal 和外设/显示/GE MSH 测试命令。未使用的 UART2/4、I2C0/1/2 等实例保留在 Kconfig 中，可按需启用。

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
export AIC_SDK_DIR=/path/to/luban-lite
python tools/mk_firmware.py --sdk "$AIC_SDK_DIR" \
    --elf rtthread.elf --out images
```

Windows PowerShell 将 `export` 换成
`$env:AIC_SDK_DIR = "...\luban-lite"` 即可。完整依赖、macOS/Linux/Windows
命令和测试见 [15-build-pack-deploy.md](15-build-pack-deploy.md)。

最终镜像：

```text
images/d21x_d213_demo128_page_2k_block_128k_v1.0.0.img
```

打包不会写设备，也不会修改 Bootloader 或 NAND 分区表。

## 1.7 串口与调试入口

- 串口参数：115200、8 数据位、无校验、1 停止位、无流控；
- 上电后应先看到 ArtInChip Bootloader 日志，再看到 `RT-Thread 5.4.0`；
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
  - `adc probe gpai` + `adc read 6`：读取 KEYADC 原始值（KEYADC 线程已使能通道 6；也可用 `adc voltage 6` 直接读电压）；
  - `keyadc`：查看最新按键采样和按下次数；`keyadc monitor` 切换事件打印；
  - `wdt_test feed|start|stop`：喂狗/启动/停止；
  - `disp_colorbar 1|0`：打开/关闭 DE 硬件彩条；
  - `disp_info` / `disp_test`：显示参数、双缓冲、VSync；
  - `ge_test`：GE FillRect/BitBlt/缩放/混合/旋转；
  - `lvgl_demo`：启动 LVGL 9.6.0 显示/点击/拖动演示（需要外部 LVGL 源码）；
  - `lvgl_touch`：统计 LVGL 触摸坐标范围，验证方向。

## 1.8 重要限制

- 实机验证状态以 `03-hardware.md` 和 `22-utest-regression.md` 的逐项
  记录为准；仅编译通过不代表实机通过。
- CAN 需要外部收发器和总线终端；仅编译通过不代表总线通信通过。
- MIPI/RGB 显示、USB OTG、AMIC 采集、I2S、视频尚未接入（DMIC 采集和
  USB0 CDC ACM 已实现）。
- `../libraries/soc/d21x/hal` 中的原厂文件保持原样，但依赖由本仓库的精简兼容层提供；升级 HAL 时必须重新核对依赖。
- 不要手工修改 `rtconfig.h`，也不要为了让某个驱动“先跑起来”而关闭 Cache 或修改 Bootloader 分区。

## 1.9 相关文档

- 首次移植新外设：[16-porting-guide.md](16-porting-guide.md)
- 构建/打包失败：[15-build-pack-deploy.md](15-build-pack-deploy.md)
- 外设不工作：[17-testing-troubleshooting.md](17-testing-troubleshooting.md)
- 原厂代码来源和许可证：[20-vendor-source-license.md](20-vendor-source-license.md)
