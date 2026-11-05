# 03 D21x 硬件与 D213EC-DEMO-V4-0 开发板

## 3.1 资料来源与可信度

本文的硬件结论来自以下资料，不使用“SDK 目录存在”推断 SoC 能力：

- ArtInChip D21x 产品功能页；
- ArtInChip D21x 型号对比页；
- ArtInChip D21x 数据手册；
- ArtInChip D213EC-DEMO-V4-0 开发板说明；
- Luban-Lite D21x SoC 头文件、DTS/板级 `pinmux.c`、`Kconfig.chip` 和官方 defconfig；
- D213EC-DEMO-V4-0 实际启动日志。

官方入口：

- <https://aicdoc.artinchip.com/topics/product/d21x-featrure-d21x.html>
- <https://aicdoc.artinchip.com/topics/product/d21x-product-compare-d21x.html>
- <https://aicdoc.artinchip.com/topics/product/d21x-dev-board-quickstart-d21x.html>
- <https://aicdoc.artinchip.com/topics/product/d213ecV4.html>
- <https://aicdoc.artinchip.com/out/downloads/D21x_Datasheet.pdf>

> 本文只对“仓库中实际验证过的 D213 demo128-nand”给出确定结论。D211/D213 的具体型号差异、封装差异和外设实例裁剪，必须以对应型号的数据手册和板级原理图为准。

## 3.2 D21x 系列定位

D21x 是 ArtInChip 的工业级高清显示与智能控制 SoC 系列，公开资料中常见的型号包括 D211、D213。两者同属 D21x 软件平台，但显示、视频、封装和外设裁剪并不完全相同。

本 BSP 的现状：

- 只为 **D213EC-DEMO-V4-0（demo128-nand 配置）** 建立了工程；
- D211 没有独立 BSP；
- 不把 D213 的显示/视频能力自动套用到 D211；
- 不根据 Luban-Lite 中 `hal/ve`、`hal/ge`、`hal/display` 等目录的存在，声称当前 BSP 已支持这些功能。

## 3.3 D213EC-DEMO-V4-0 板级资源

以下资源来自官方开发板说明和原厂 `target/d21x/demo128-nand` 配置：

| 资源 | 说明 |
|---|---|
| SoC | D213 |
| DDR | 128 MB DDR3 |
| SPI NAND | 128 MB，QSPI0，page 2K / block 128K |
| 显示 | 7 英寸 LVDS + 电容触摸；10.1 英寸 MIPI DSI + 电容触摸（按套装） |
| 存储扩展 | SD 卡 |
| RTC | 板载 RTC |
| USB | USB0、USB1 |
| 以太网 | 100M Ethernet |
| Wi-Fi | 外部 Wi-Fi 模组（不是 SoC 原生） |
| 音频 | DSPK、DMIC |
| 工业串口 | RS485 ×2、RS232 ×2 |
| CAN | CAN0、CAN1（板上有收发器） |
| 调试串口 | UART0，PA.0/PA.1 |

**注意：** “板上有 CAN 收发器”不等于“已完成 CAN 总线测试”。还需要正确的总线拓扑、120 Ω 终端、对端节点和电源。

## 3.4 地址空间

| 模块 | 基地址 | 大小/备注 |
|---|---|---|
| DDR | `0x40000000` | 128 MB |
| CMU | `0x18020000` | 时钟/复位 |
| GPIO | `0x18700000` | PA..PG（PU 特殊组未在 Pin 设备中暴露） |
| UART0 | `0x18710000` | 控制台 |
| UART1 | `0x18711000` | RS-232 |
| UART2 | `0x18712000` | 与 I2C2 共用 PA.8/9 |
| UART3 | `0x18713000` | 板级未引出 |
| UART4 | `0x18714000` | 与 SDMC0/QSPI0 共用 PB.6/7 |
| UART5 | `0x18715000` | 板级未引出 |
| UART6 | `0x18716000` | 板级未引出 |
| UART7 | `0x18717000` | 板级未引出 |
| WDT | `0x19000000` | 看门狗 |
| RTC | `0x19030000` | RTC |
| I2C0 | `0x19220000` | PD.6/PD.7 |
| I2C1 | `0x19221000` | PD.18/PD.19 |
| I2C2 | `0x19222000` | PA.8/PA.9 |
| I2C3 | `0x19223000` | PA.10/PA.11，触摸 |
| CAN0 | `0x19230000` | PE.16/PE.17 |
| CAN1 | `0x19231000` | PE.18/PE.19 |
| PWM | `0x19240000` | 4 通道，PC.0..PC.7 |
| PLIC | `0x20000000` | 机器模式外部中断 |
| CLINT | `0x24000000` | 机器定时器 |

## 3.5 中断号

| 中断 | 编号 | 当前使用 |
|---|---|---|
| UART0 | 76 | 是 |
| UART1 | 77 | 可选 |
| UART2 | 78 | 可选 |
| UART3 | 79 | 可选（无板级引脚） |
| UART4 | 80 | 可选 |
| UART5 | 81 | 可选（无板级引脚） |
| UART6 | 82 | 可选（无板级引脚） |
| UART7 | 83 | 可选（无板级引脚） |
| I2C0 | 84 | 可选（轮询，不注册 ISR） |
| I2C1 | 85 | 可选 |
| I2C2 | 86 | 可选 |
| I2C3 | 87 | 可选 |
| CAN0 | 88 | 可选 |
| CAN1 | 89 | 可选 |
| PWM | 90 | 未使用（PWM 中断未接入） |
| WDT | 64 | 未使用（仅复位/喂狗） |
| RTC | 50 | 未使用（闹钟未实现） |
| GPIO | 68-75 | PA..PH 每组一个 |

## 3.6 时钟与复位

D21x 的 PLL 树由 ArtInChip Bootloader 初始化。BSP 不重新编程 PLL，只做外设门控和复位释放：

- `CLK_BUS_GATE = BIT(12)`：总线时钟门控；
- `CLK_MOD_GATE(bit)`：模块时钟门控；
- `CLK_RSTN = BIT(13)`：复位释放。

已确认的 D21x 外设门控：

| 外设 | 寄存器偏移 | 总线门控 | 模块门控 | 输入频率 |
|---|---|---|---|---|
| GPIO | `0x083C` | bit 12 | 无 | APB0 |
| UART0-7 | `0x0840 + n*4` | bit 12 | bit 8 | 48 MHz |
| I2C0-3 | `0x0960 + n*4` | bit 12 | 无 | 24 MHz（APB1） |
| CAN0/1 | `0x0980 + n*4` | bit 12 | 无 | 24 MHz（APB1） |
| PWM | `0x0990` | bit 12 | bit 8 | 48 MHz |
| WDT | `0x020C` | bit 12 | bit 8 | 32.768 kHz |
| RTC | `0x0908` | bit 12 | 无 | 32.768 kHz |
| GTC | `0x090C` | bit 12 | 无 | APB1 |

这些频率是 BSP 的固定假设，来源于原厂 `target/d21x/demo128-nand/sys_clk.c` 和 UART/PWM Kconfig 默认值。如果产品修改了 PLL 或分频，必须同步修改 `board/Kconfig` 和 `vendor/port/aic_hal_clk.c`。

## 3.7 Pinmux

D21x 每个引脚有独立的 pin configuration 寄存器：

```text
PIN_CFG = FUNC[3:0] | DRIVE[6:4] | PULL[9:8] |
          IRQ_MODE[14:12] | DIR[17:16] |
          SPE_IE_FORCE[18] | DEBOUNCE[31:20]
```

当前 `board/pinmux.c` 只配置 UART 引脚：

| 功能 | 引脚 | 功能号 | 上下拉 | 说明 |
|---|---|---|---|---|
| UART0_TX | PA.0 | 5 | 无 | 控制台 |
| UART0_RX | PA.1 | 5 | 上拉 | 控制台 |
| UART1_TX | PA.4 | 5 | 无 | RS-232 |
| UART1_RX | PA.5 | 5 | 上拉 | RS-232 |
| UART2_TX | PA.8 | 5 | 无 | 与 I2C2 冲突 |
| UART2_RX | PA.9 | 5 | 上拉 | 与 I2C2 冲突 |
| UART4_TX | PB.6 | 5 | 无 | 与 SDMC0/QSPI0 冲突 |
| UART4_RX | PB.7 | 5 | 无 | 与 SDMC0/QSPI0 冲突 |
| I2C0 | PD.6/PD.7 | 4 | 无 | 外部 I2C |
| I2C1 | PD.18/PD.19 | 5 | 无 | 外部 I2C |
| I2C3 | PA.10/PA.11 | 4 | 无 | 触摸 |
| CAN0 | PE.16/PE.17 | 4 | 无 | 收发器 |
| CAN1 | PE.18/PE.19 | 4 | 无 | 收发器 |
| PWM0 | PC.0/PC.1 | 7 | 无 | 与 SDMC1 共用 |
| PWM1 | PC.2/PC.3 | 7 | 无 | 与 SDMC1 共用 |
| PWM2 | PC.4/PC.5 | 7 | 无 | 与 SDMC1 共用 |
| PWM3 | PC.6/PC.7 | 7 | 无 | 与 SDMC1 共用 |

**冲突规则：** 同时启用 UART2 与 I2C2、UART4 与 SDMC0/QSPI0、PWM 与 SDMC1，会导致后配置的外设覆盖先配置的引脚。Kconfig 不会自动阻止这些组合；产品必须自行选择。

## 3.8 外设支持矩阵

“BUILD”表示该驱动已纳入仓库并通过编译/链接检查；默认 `.config` 启用 UART0/1、GPIO、I2C3、CAN0/1、PWM、WDT、RTC、DE/LVDS Framebuffer、双缓冲和 GE Normal。其他实例通过 `scons --menuconfig` 按需启用。

| 外设 | SoC 支持 | 开发板资源 | 原厂实现 | RT-Thread 接口 | 当前实现 | 验证状态 | 已知限制 |
|---|---|---|---|---|---|---|---|
| CPU C906 | 是 | 是 | `libcpu` 上游 | 内核 | 完整 | HW（待回归） | `fcsr` 未保存，见 04 章 |
| PLIC | 是 | 是 | 本 BSP | 内核 | 完整 | HW（待回归） | 128 IRQ 表 |
| CLINT Tick | 是 | 是 | 本 BSP | 内核 | 完整 | HW（待回归） | 4 MHz 固定 |
| UART0 | 是 | 是 | 本 BSP + HAL 参考 | serial v1 | 完整 | HW（待回归） | 无 DMA/流控 |
| UART1/2/4 | 是 | UART1/2/4 引出 | 本 BSP | serial v1 | 完整 | BUILD/STATIC | UART2/4 引脚冲突 |
| UART3/5/6/7 | 是 | 未引出 | 本 BSP | serial v1 | 可编译 | BUILD | 无板级 pinmux |
| GPIO | 是 | 是 | `aic_hal_gpio.c` | pin | 完整 | BUILD/STATIC | 开漏仅按普通输出处理 |
| GPIO 中断 | 是 | 是 | `aic_hal_gpio.c` | pin IRQ | 完整 | BUILD/STATIC | 仅 PA..PG |
| I2C0-3 | 是 | I2C0/1/3 可用 | `hal_i2c.c` | i2c | 仅主机 | BUILD/STATIC | 从机未注册；10-bit 需配置 |
| Touch (GT911) | 是 | I2C3，PA.8 复位，PA.9 中断 | 本 BSP | touch | GT911 5 点 | HW（多点读写通过） | 方向/中断模式待校准 |
| CAN0/1 | 是 | 是 | `aic_hal_can.c` | can | 完整 | BUILD/STATIC | 仅 CAN 2.0A/B；无 CAN FD |
| PWM0-3 | 是 | PC.0..PC.7 | `hal_pwm.c` | pwm | 4 通道 | BUILD/STATIC | 无 EPWM/CAP/QEP |
| WDT | 是 | 是 | `hal_wdt.c` | watchdog | 完整 | BUILD/STATIC | 未接中断回调 |
| RTC | 是 | 是 | `hal_rtc.c` | rtc | 秒计数 | BUILD/STATIC | 闹钟未实现 |
| SPI NAND | 是 | 128 MB | 原厂 QSPI/SPINAND | MTD | 未实现 | N/I | 需 QSPI+DMA+ECC+BBM |
| SD/MMC | 是 | SD 卡 | 原厂 SDMC | SDIO/MMCSD | 未实现 | N/I | 需 SDMC + DMA |
| Display/LVDS | 是 | 7" LVDS + CTP | `aic_hal_de.c` + LVDS | graphic (`lcd`) | DE + LVDS + FB + VSync | HW | Color Bar、双缓冲、VSync 已实机验证 |
| Display/MIPI/RGB | 是 | 10.1" MIPI 套装 | 原厂 DE/DSI/RGB | framebuffer | 未实现 | N/I | 需面板时序/lane 配置 |
| GE | 是 | — | `hal_ge_normal.c` | 自定义 `ge` | Normal Fill/BitBlt/Scale/Blend/Rotate | HW（Fill/BitBlt）；缩放/混合/旋转 BUILD/STATIC | 无 CMD Queue；仅 RGB；混合用例待复测 |
| LVGL | — | 7" LVDS + CTP | LVGL v9.6.0 + lvgl-aic-standalone | 外部组件 display + indev | 软件渲染 + 可选 GE 帧拷贝 + GT911 输入 | BUILD + PACKAGE（新组件待实机） | 未提交 LVGL/适配组件源码；旧 BSP 内端口曾 HW 验证，结论不自动转移 |
| USB0/1 | 是 | 是 | 原厂 USB/CherryUSB | USB 栈 | 未实现 | N/I | 需 PHY/角色配置 |
| Ethernet | 是 | 100M | 原厂 GMAC | netdev/LwIP | 未实现 | N/I | 需 PHY/MDIO |
| Wi-Fi | 否（外模组） | 模组 | 第三方 | WLAN | 未实现 | N/I | 不是 SoC 原生 |
| Audio | 是 | DSPK/DMIC | 原厂 I2S/Codec | audio | 未实现 | N/I | 需 DMA/Codec |
| Video/VE | 按型号 | — | 原厂 VE | 自定义 | 未实现 | N/I | 不声称编码能力 |
| ADC/PSADC | 是 | 按板卡 | 原厂 | ADC | 未实现 | N/I | 需 DMA/校准 |
| EPWM/CAP/QEP | 是 | 按板卡 | 原厂 | 自定义 | 未实现 | N/I | 不等同于 PWM |
| CIR/eFuse/CE | 是 | 按板卡 | 原厂 | 自定义 | 未实现 | N/I | 未验证 |

## 3.9 启动介质与顺序

D21x 的 Boot ROM 启动顺序（原厂资料）：

1. SD Card（SDMC1）
2. SPI NAND（QSPI0）
3. SPI NOR（QSPI0）
4. eMMC（SDMC0）

USB 和 SD 升级路径由原厂 Bootloader 提供。RT-Thread 镜像不修改这个顺序，也不直接操作 NAND 分区表。

## 3.10 型号差异的维护要求

新增 D211 或 D213 变体时，必须先完成以下核对，不能复制粘贴本表：

1. 数据手册中的外设实例数量、DMA 请求号、中断号；
2. 封装引脚和 pinmux 功能号；
3. 显示接口（RGB/LVDS/MIPI）和最大分辨率；
4. 视频/GE/VE 是否存在及其能力；
5. ADC/PSADC 通道数；
6. USB 控制器数量和 PHY 配置；
7. 以太网 MAC 数量和 PHY 接口；
8. 启动介质与 Bootloader 镜像是否匹配。

在完成这些核对前，新增型号只能标记为 N/I 或 SRC，不能标记为 HW。
