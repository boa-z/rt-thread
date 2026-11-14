# 03 D21x 硬件与 D213ECV-DEMO-V4-0 开发板

## 3.1 资料来源与可信度

本文的硬件结论来自以下资料，不使用“SDK 目录存在”推断 SoC 能力：

- ArtInChip D21x 产品功能页；
- ArtInChip D21x 型号对比页；
- ArtInChip D21x 数据手册；
- ArtInChip D213ECV-DEMO-V4-0 开发板说明；
- Luban-Lite D21x SoC 头文件、DTS/板级 `pinmux.c`、`Kconfig.chip` 和官方 defconfig；
- D213ECV-DEMO-V4-0 实际启动日志。

官方入口：

- <https://aicdoc.artinchip.com/topics/product/d21x-featrure-d21x.html>
- <https://aicdoc.artinchip.com/topics/product/d21x-product-compare-d21x.html>
- <https://aicdoc.artinchip.com/topics/product/d21x-dev-board-quickstart-d21x.html>
- <https://aicdoc.artinchip.com/topics/product/d213ecV4.html>
- <https://aicdoc.artinchip.com/out/downloads/D21x_Datasheet.pdf>

> 本文只对“仓库中实际验证过的 D213ECV-DEMO-V4-0（`demo128-nand` 原厂配置）”给出确定结论。D211/D213 的具体型号差异、封装差异和外设实例裁剪，必须以对应型号的数据手册和板级原理图为准。

## 3.2 D21x 系列定位

D21x 是 ArtInChip 的工业级高清显示与智能控制 SoC 系列，公开资料中常见的型号包括 D211、D213。两者同属 D21x 软件平台，但显示、视频、封装和外设裁剪并不完全相同。

本 BSP 的现状：

- 只为 **D213ECV-DEMO-V4-0（demo128-nand 配置）** 建立了工程；
- D211 没有独立 BSP；
- 不把 D213 的显示/视频能力自动套用到 D211；
- 不根据 Luban-Lite 中 `hal/ve`、`hal/ge`、`hal/display` 等目录的存在，声称当前 BSP 已支持这些功能。

## 3.3 D213ECV-DEMO-V4-0 板级资源

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
| 以太网 | 100M Ethernet，RTL8201F（RMII）；PE.6 复位，PE.10/CLK_OUT2 提供 25 MHz |
| Wi-Fi | RTL8733BS Wi-Fi+BT 模组，SDIO 接 SDMC2（不是 SoC 原生） |
| 音频 | DSPK、DMIC |
| 工业串口 | RS485 ×2、RS232 ×2 |
| CAN | CAN0、CAN1（板上有收发器） |
| 按键 | K4 UP、K5 DOWN、K6 LEFT、K7 RIGHT，ADC 梯形电阻接 PA.6/KEYADC；另有 RESET、UBOOT（PA.0）、WAKEUP（PF.15） |
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
| UART7 | `0x18717000` | BT HCI，PF.6..PF.9（模组） |
| WDT | `0x19000000` | 看门狗 |
| RTC | `0x19030000` | RTC |
| I2C0 | `0x19220000` | PD.6/PD.7 |
| I2C1 | `0x19221000` | PD.18/PD.19 |
| I2C2 | `0x19222000` | PA.8/PA.9 |
| I2C3 | `0x19223000` | PA.10/PA.11，触摸 |
| CAN0 | `0x19230000` | PE.16/PE.17 |
| CAN1 | `0x19231000` | PE.18/PE.19 |
| PWM | `0x19240000` | 4 通道，PC.0..PC.7 |
| ADCIM | `0x19250000` | 模拟公共模块，TSEN/GPAI 共用 |
| GPAI | `0x19251000` | 通用 ADC，8 通道；本板使用 GPAI6/PA.6 |
| TSEN | `0x19253000` | 片内温度传感器 |
| SID/eFuse | `0x19010000` | 芯片校准参数（只读） |
| SDMC1 | `0x10450000` | SD 卡，PC.0..PC.6 |
| SDMC2 | `0x10460000` | SDIO，PF.0..PF.5 |
| GMAC0 | `0x10280000` | 100M RMII，MDIO，IRQ 39 |
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
| UART7 | 83 | 可选（BT HCI，未注册 ISR） |
| I2C0 | 84 | 可选（轮询，不注册 ISR） |
| I2C1 | 85 | 可选 |
| I2C2 | 86 | 可选 |
| I2C3 | 87 | 可选 |
| CAN0 | 88 | 可选 |
| CAN1 | 89 | 可选 |
| PWM | 90 | 未使用（PWM 中断未接入） |
| GPAI | 92 | 是（`BSP_USING_GPAI6` 时注册数据就绪中断） |
| WDT | 64 | 未使用（仅复位/喂狗） |
| DMA | 32 | 可选（`BSP_USING_DMAC` 时注册 ISR） |
| RTC | 50 | 是（`RT_USING_ALARM` 时注册闹钟中断） |
| TSEN | 94 | 是（`BSP_USING_TSEN` 时注册数据就绪中断） |
| SDMC1 | 47 | 可选（仅 SDIO “卡请求注意”源） |
| SDMC2 | 48 | 可选（仅 SDIO “卡请求注意”源） |
| GMAC0 | 39 | 是（RX 完成中断） |
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
| DMA | `0x0410` | bit 12 | 无 | AHB0 |
| WDT | `0x020C` | bit 12 | bit 8 | 32.768 kHz |
| RTC | `0x0908` | bit 12 | 无 | 32.768 kHz |
| TSEN | `0x09AC` | bit 12 | 无 | APB1 24 MHz |
| ADCIM | `0x09A0` | bit 12 | bit 8 | OSC24M 分频（bootloader 配置） |
| GPAI | `0x09A4` | bit 12 | 无 | APB1 24 MHz |
| SID | `0x0904` | bit 12 | bit 8 | OSC24M |
| CE | `0x0418` | bit 12 | bit 8 | PLL_INT1 分频（bootloader 配置） |
| GTC | `0x090C` | bit 12 | 无 | APB1 |

这些频率是 BSP 的固定假设，来源于原厂 `target/d21x/demo128-nand/sys_clk.c` 和 UART/PWM Kconfig 默认值。如果产品修改了 PLL 或分频，必须同步修改 `board/Kconfig` 和 `../libraries/soc/d21x/port/aic_hal_clk.c`。

UART 分频字段在 `0x0840 + n*4` 的 bits[4:0]。Bootloader 只配置 UART0，
UART1..7 该字段为 0（模块时钟停止，寄存器读回 0）；`board/clock.c` 在使能
门控前复制 UART0 的分频值，见 07 章 7.3.1。

## 3.7 Pinmux

D21x 每个引脚有独立的 pin configuration 寄存器：

```text
PIN_CFG = FUNC[3:0] | DRIVE[6:4] | PULL[9:8] |
          IRQ_MODE[14:12] | DIR[17:16] |
          SPE_IE_FORCE[18] | DEBOUNCE[31:20]
```

启用对应外设时，`board/pinmux.c` 配置 UART、I2C、CAN、PWM、GPAI 和
SDMC1 引脚；下表列出已实现的组合：

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
| UART7 | PF.6..PF.9 | 5 | TX/RTS 无，RX/CTS 上拉 | BT HCI；仅 `BSP_USING_UART7` 时配置 |
| I2C0 | PD.6/PD.7 | 4 | 无 | 外部 I2C |
| I2C1 | PD.18/PD.19 | 5 | 无 | 外部 I2C |
| I2C3 | PA.10/PA.11 | 4 | 无 | 触摸 |
| CAN0 | PE.16/PE.17 | 4 | 无 | 收发器 |
| CAN1 | PE.18/PE.19 | 4 | 无 | 收发器 |
| KEYADC / GPAI6 | PA.6 | 2 | 无（板级 10 k 上拉） | K4-K7 梯形电阻 |
| PWM0 | PC.0/PC.1 | 7 | 无 | 与 SDMC1 共用 |
| PWM1 | PC.2/PC.3 | 7 | 无 | 与 SDMC1 共用 |
| PWM2 | PC.4/PC.5 | 7 | 无 | 与 SDMC1 共用 |
| PWM3 | PC.6/PC.7 | 7 | 无 | 与 SDMC1 共用 |
| SDMC1 | PC.0..PC.6 | 2 | 上拉，驱动 3 | 4-bit SD 卡；仅 `BSP_USING_SDMC1` 时配置 |
| SDMC2 | PF.0..PF.5 | 2 | 上拉，驱动 3 | 4-bit SDIO（RTL8733BS）；仅 `BSP_USING_SDMC2` 时配置 |
| GMAC0 | PE.0..PE.5、PE.7..PE.9 | 6 | 无 | RTL8201F RMII；PE.6 复位，PE.10 CLK_OUT2 25 MHz |

**冲突规则：** 同时启用 UART2 与 I2C2、UART4 与 SDMC0/QSPI0、PWM 与
SDMC1，会导致后配置的外设覆盖先配置的引脚。UART2/I2C2、UART4/SDMC0
仍需产品自行选择；`BSP_USING_SDMC1` 已通过 `depends on !BSP_USING_PWM`
阻止与 PWM 同时启用。PC.7（面板使能）不属于 SDMC1 的 4-bit 引脚组。
SDMC2 的 PF.0..PF.5 与 BT UART7（PF.6..PF.9）不重叠。

## 3.8 外设支持矩阵

“BUILD”表示该驱动已纳入仓库并通过编译/链接检查；默认 `.config` 启用 UART0/1、GPIO、I2C3、CAN0/1、PWM、WDT、RTC、GPAI6/KEYADC、DE/LVDS Framebuffer、双缓冲和 GE Normal。其他实例通过 `scons --menuconfig` 按需启用。

| 外设 | SoC 支持 | 开发板资源 | 原厂实现 | RT-Thread 接口 | 当前实现 | 验证状态 | 已知限制 |
|---|---|---|---|---|---|---|---|
| CPU C906 | 是 | 是 | `libcpu` 上游 | 内核 | 完整 | HW PASS（HIL `20261108-092325-14520`） | `fcsr`/`fs0..fs11` 未保存，见 04 章、22.6 |
| PLIC | 是 | 是 | 本 BSP | 内核 | 完整 | HW PASS（HIL `20261108-092325-14520`） | 128 IRQ 表 |
| CLINT Tick | 是 | 是 | 本 BSP | 内核 | 完整 | HW PASS（HIL `20261108-092325-14520`） | 4 MHz 固定 |
| UART0 | 是 | 是 | 本 BSP + HAL 参考 | serial v1 | 完整 | HW PASS（HIL `20261108-092325-14520`） | 无流控；TX 可走 DMA |
| UART1/2/4 | 是 | UART1/2/4 引出 | 本 BSP | serial v1 | 完整 | UART1 TX DMA 9/9 HW PASS（HIL `20261108-020309-21624`：单包、连续分片、通道回退、关闭、并发 RX；共享层重构后复测 `20261109-093337-15756`）；UART2/4 BUILD/STATIC | UART2/4 引脚冲突；RX 为 1 ms 轮询 |
| UART3/5/6 | 是 | 未引出 | 本 BSP | serial v1 | 可编译 | BUILD | 无板级 pinmux |
| UART7 | 是 | BT HCI（PF.6..PF.9） | 本 BSP | serial v1 | 4 线 TX/RX/RTS/CTS，可选自动流控 | HW PASS（`bsp.d213.uart7`，HIL `20261108-064913-21800`） | RX 为 1 ms 轮询；RTS/CTS 未经模组对端验证 |
| GPIO | 是 | 是 | `aic_hal_gpio.c` | pin | 完整 | HW（`bsp.d213.gpio` PASS；外部负载/波形未验证） | 开漏仅按普通输出处理 |
| GPIO 中断 | 是 | 是 | `aic_hal_gpio.c` | pin IRQ | 完整 | BUILD/STATIC | 仅 PA..PG |
| I2C0-3 | 是 | I2C0/1/3 可用 | `hal_i2c.c` | i2c | 仅主机 | I2C3 HW PASS（GT911）；I2C0/1/2 BUILD/STATIC | 从机未注册；10-bit 需配置 |
| Touch (GT911) | 是 | I2C3，PA.8 复位，PA.9 中断 | 本 BSP | touch | GT911 5 点 | HW（多点读写通过） | 方向/中断模式待校准 |
| CAN0/1 | 是 | 是 | `aic_hal_can.c` | can | 完整 | HW（`bsp.d213.can0.config` PASS；外部总线 SKIP） | 仅 CAN 2.0A/B；无 CAN FD |
| PWM0-3 | 是 | PC.0..PC.7 | `hal_pwm.c` | pwm | 4 通道 | HW（`bsp.d213.pwm` PASS；波形未验证） | 无 EPWM/CAP/QEP |
| WDT | 是 | 是 | `hal_wdt.c` | watchdog | 完整 | HW PASS（start/stop/feed/timeleft/复位注入，报告 `20261109-055217`；存储 soak `20261109-035532`） | 未接中断回调；清零仅在 clr_thd..rst_thd 窗口内生效 |
| RTC | 是 | 是 | `hal_rtc.c` | rtc + alarm | 秒计数、一次性/周期闹钟 | HW PASS（`bsp.d213.rtc`，HIL `20261108-085811-23408`） | 无掉电唤醒/备份域验证 |
| TSEN | 是 | 片内 | `hal_tsen.c` + `hal_adcim.c` + `hal_efuse.c` | sensor v1 | CPU 通道单次采样、eFuse 校准 | HW PASS（`bsp.d213.tsen`，HIL `20261108-092325-14520`） | 仅 CPU 通道；TSEN 的 GPAI 通道、周期采样和 HTA/LTA/OTP 报警未实现；精度未标定 |
| CE（加密引擎） | 是 | 片内 | `hal_ce.c`（原厂，逐字节） | hwcrypto | AES-128/192/256 ECB/CBC、MD5、SHA-1/224/256/384/512，轮询方式 | HW PASS（`bsp.d213.ce` 5/5，HIL `20261109-034918-80696`） | 无中断、DES/3DES、SM2/SM4、HMAC、TRNG；RSA 代码未启用；未做性能测试 |
| SPI NAND | 是 | 128 MB | 本 BSP `drv_qspi.c` + `drv_spinand.c`（参考原厂 QSPI/SPINAND）+ `drv_nand_rodata.c`/`drv_nand_lfs.c` | MTD NAND + QSPI + DFS | Page/OOB/ECC/坏块检查 + Program/Erase/Move/Mark-Bad；声明分区默认只读，未分配尾部可注册为可写 `nand_scratch`；`/rodata` 只读 FAT，`/nand` LittleFS + 持久化 BBT | BUILD + STATIC + HOST；读取 HW PASS；写/擦/移动/标坏在 `nand_scratch` 上 HW PASS（`bsp.d213.spinand.ro`/`.rw`，HIL `20261108-230606-16252`）；文件系统 HW PASS（`bsp.d213.spinand.fs`，HIL `20261109-002129-38512`） | 声明分区写入未验证；无 Refresh、NFTL、OTA 更新引擎（只有 OTA/A-B 只读诊断）；文件系统仅限 `nand_scratch`，无掉电注入。`data/data_r` 为 NFTL，未注册为线性 MTD |
| SD/MMC | 是 | SD 卡（SDMC1）；RTL8733BS SDIO 模组（SDMC2） | `hal_sdmc.c`（原厂 SDMC，含 IDMA） | MMCSD + block + SDIO | 双实例：4-bit、IDMA/CPU FIFO、SDR25；SDIO CMD52/CMD53、CCCR/FBR/CIS、attach_irq | SDMC1：卡识别/块设备/只读 utest/FAT 挂载/IDMA 64 扇区回归 HW PASS；SDMC2：BUILD + STATIC + PACKAGE，SDIO utest 见 12.15 | SDMC1 无热插拔且与 PWM 引脚互斥；SDMC2 需要模组供电和 WL_REG_ON |
| DMA (DMAC) | 是 | — | `hal_dma.c`（原厂 DMA v1.0） | 自定义 8 通道通道 API | mem-to-mem/memset/device/cyclic + IRQ 32 | HW PASS（`bsp.d213.dmac` 7/7；1 MiB 170666 KB/s；共享层重构后复测 `20261109-093211-17328`） | 已接入 UART TX、音频播放和 DMIC 采集；无通道仲裁/PM；SPI/I2S 待接入 |
| Display/LVDS | 是 | 7" LVDS + CTP | `aic_hal_de.c` + LVDS | graphic (`lcd`) | DE + LVDS + FB + VSync | HW | Color Bar、双缓冲、VSync 已实机验证 |
| Display/MIPI/RGB | 是 | 10.1" MIPI 套装 | 原厂 DE/DSI/RGB | framebuffer | 未实现 | N/I | 需面板时序/lane 配置 |
| GE | 是 | — | `hal_ge_normal.c`（含错误恢复和资源清理） | 自定义 `ge` | Normal Fill/BitBlt/Scale/Blend/Rotate | HW（Fill/BitBlt/混合/2× 缩放/90° 顺时针旋转像素校验，20 轮回归 `20261109-063037`；共享层重构后复测 `20261109-092840`） | 无 CMD Queue；仅 RGB；任意角度/翻转/其他缩放比例未验证 |
| LVGL | — | 7" LVDS + CTP | LVGL v9.6.0 + lvgl-aic-standalone | 外部组件 display + indev | 软件渲染 + 可选 GE 帧拷贝 + GT911 输入 | BUILD + PACKAGE（新组件待实机） | 未提交 LVGL/适配组件源码；旧 BSP 内端口曾 HW 验证，结论不自动转移 |
| USB0/1 | 是 | 是 | 原厂 USB glue/UDC + CherryUSB | host + MSC + FAT；device + CDC ACM | EHCI/OHCI Host、U 盘块设备与自动挂载；USB0 CDC ACM 字符设备 | Host HW PASS（USB1，`bsp.d213.usb`，HIL `20261108-074359-19904`）；Device HW PASS（枚举 + CDC 双向 MSH，HIL `20261109-035704-41120`；`usb_dev_console on/off` 连续 16 轮，HIL `20261109-053213-7196`、回归 `20261109-053348`） | 同一镜像只启用一个端口；OHCI 低速设备未实机；USB0 Host/Device 互斥；OTG 未实现 |
| Ethernet | 是 | 100M，RTL8201F | 原厂 GMAC + PHY HAL | netdev/LwIP | RMII、MDIO、PHY 轮询、DHCP | HW PASS（`bsp.d213.eth`，HIL `20261108-085007-17888`） | 仅 GMAC0；无 RGMII 和热插拔；未做吞吐/长时间压力测试 |
| Wi-Fi | 否（外模组） | RTL8733BS（SDIO 2.0，SDMC2） | 第三方 | WLAN | Host 侧已实现；协议栈不在 BSP | BUILD + STATIC（`bsp.d213.sdio`） | 模组固件/驱动不在本仓库；BT HCI 端口（UART7）已提供，协议栈不在本仓库 |
| Audio | 是 | SPK1 + PA_SHDN（PE.11 / PA.7），DMIC PE.12/PE.13 | `hal_audio.c`（原厂 Codec）+ PLL_FRA1 | RT-Thread audio（`sound0`、`dmic0`） | 16-bit 8–48 kHz 播放，循环 DMA，PA 控制；DMIC 采集（1/2 声道，record pipe） | HW（`bsp.d213.audio`、`bsp.d213.dmic` PASS；DMIC 声学输入已用扬声器实测；共享层重构后复测 HIL `20261109-093546-7576`） | 只支持 16-bit；DMIC 在 32 kHz 以上超出板上麦克风时钟范围，建议 8–24 kHz；I2S 未接入；扬声器输出仍需人耳确认 |
| Video/VE | 按型号 | — | 原厂 VE | 自定义 | 未实现 | N/I | 不声称编码能力 |
| ADC（GPAI） | 是 | PA.6 按键梯形 | `hal_gpai.c` + `hal_adcim.c`（原厂，逐字节） | adc | 单通道单次采样，12 位，CPU 中断完成 | BUILD/STATIC + `bsp.d213.keyadc`；实机采样待验证 | 仅 GPAI6；无 DMA、周期采样、HTA/LTA 报警；PSADC 未实现 |
| 按键（KEYADC） | 是 | K4-K7 UP/DOWN/LEFT/RIGHT | 本 BSP `drv_key.c` | 自定义回调 + `keyadc` MSH | GPAI6 轮询、30 ms 消抖、按下/松开/长按 | BUILD/STATIC + `bsp.d213.keyadc`；实机按键待验证 | 梯形电阻无法识别多键同时按下；长按阈值 1 s |
| EPWM/CAP/QEP | 是 | 按板卡 | 原厂 | 自定义 | 未实现 | N/I | 不等同于 PWM |
| CIR/eFuse 用户 API | 是 | 按板卡 | 原厂 | 自定义 | 未实现（TSEN 仅用 eFuse 只读校准） | N/I | 未验证 |

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
