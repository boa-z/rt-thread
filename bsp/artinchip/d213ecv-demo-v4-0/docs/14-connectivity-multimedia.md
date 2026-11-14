# 14 连接、多媒体与扩展外设

## 14.1 当前状态

本 BSP 当前实现了 GPIO、UART、I2C、CAN、PWM、WDT、RTC、SDMC/SDIO、
BT HCI 端口、音频播放与 DMIC 采集、USB Host（EHCI/OHCI + MSC/FAT）、
USB Device（USB0 CDC ACM）和 Ethernet（GMAC0 + RTL8201F）。

以下功能**未实现（N/I）**：

- USB OTG 角色自动切换、USB Device 的其他类（HID/MSC/音频）；
- eMMC、SDIO Wi-Fi 协议栈；
- Wi-Fi/蓝牙协议栈；
- 音频采集 AMIC 与 I2S；
- DVP 摄像头；
- 视频引擎（VE）；
- PSADC/GPADC；
- EPWM/CAP/QEP；
- CIR；
- eFuse/SID 用户 API；
- 加密引擎；
- 电源管理/休眠唤醒。

本文说明硬件能力、原厂代码位置、接入依赖和验证要求，避免把“SDK 有目录”当成“BSP 支持”。

## 14.2 USB

### 硬件

D21x 提供 USB2.0 控制器（USB0/USB1，按型号/板卡配置）。D213 demo128 官方 defconfig：

```text
CONFIG_AIC_USING_USB0=y
CONFIG_AIC_USING_USB0_HOST=y
# CONFIG_AIC_USING_USB1 is not set
```

即 demo128 默认使用 USB0 Host。D213ECV-DEMO-V4-0 的 USB0 数据线是
PO.0/PO.1，USB1 是 PO.2/PO.3（都是 function 2）；没有单独的 VBUS
使能 GPIO。USB1 在 D21x 上是纯 Host，没有角色切换寄存器。

| 项目 | USB0 | USB1 |
|---|---|---|
| 控制器 | EHCI 高速主机 | EHCI 高速主机 |
| 寄存器基址 | `0x10210000` | `0x10220000` |
| 伴随 OHCI | 基址 + 0x400 | 基址 + 0x400 |
| 中断 | EHCI 35 / OHCI 36 | EHCI 37 / OHCI 38 |
| 引脚 | PO.0 / PO.1 | PO.2 / PO.3 |
| PHY 角色 | SYS_CFG `USB0_CFG[0]` = 0 选择 Host | 固定 Host |
| PHY 基准电阻 | SYS_CFG `USB0_REXT`（0x048） | SYS_CFG `USB1_REXT`（0x04C） |
| 时钟门控 | `CLK_USBH0_REG` 0x0420 / `CLK_USB_PHY0_REG` 0x0430 | `CLK_USBH1_REG` 0x0424 / `CLK_USB_PHY1_REG` 0x0434 |

两个控制器共用一套 EHCI/OHCI 驱动，但 `CONFIG_USBHOST_MAX_BUS` 为 1，
所以同一镜像只启用一个端口；Kconfig 帮助里也写明了这一点。

USB0 同时还有一个设备控制器（UDC），基址 `0x10200000`、中断 34，与
主机控制器共用 PO.0/PO.1 和 PHY；`SYS_CFG USB0_CFG[0] = 1` 选择
Device。因此 USB0 的主机/设备模式互斥，USB1 始终是 Host。

### 软件组织

```text
components/drivers/usb/cherryusb/          CherryUSB 栈（公共组件）
  port/ehci/usb_glue_aic.c                 AIC EHCI/OHCI 低层初始化（原厂）
  port/ehci/usb_hc_ehci.c                  EHCI 主机控制器
  port/ohci/usb_hc_ohci.c                  OHCI 伴随控制器
  port/aic/usb_dc_aic.c                    AIC UDC 设备端口（原厂）
  port/aic/usb_dc_aic_ll.c                 UDC 时钟/复位/中断低层
  platform/rtthread/rt_usbh_msc.c          MSC 块设备 + FAT 自动挂载
  platform/rtthread/rt_usbd_serial.c       CDC ACM → RT-Thread 字符设备
board/usb_config.h                         CherryUSB 配置（本 BSP）
../libraries/soc/d21x/drivers/drv_usb.c                          Host D-Cache 维护 + PHY 校准
board/usb_device.c                      USB0 Device 胶水 + CDC ACM 描述符
../libraries/soc/d21x/port/aic_hal_syscfg.c               USB0/USB1 REXT、PHY0 角色切换
../libraries/soc/d21x/port/aic_hal_clk.c                  CLK_SYSCFG / USBHx / USBD / USB_PHYx
../libraries/soc/d21x/port/aic_hal_reset.c                RESET_USBHx / RESET_USBD / RESET_USBPHYx
```

不复制 CherryUSB 源码；BSP 提供板级配置、时钟/复位描述、PHY 寄存器
和缓存钩子。原厂 AIC glue 通过 `syscfg_usb_phy0_sw_host()`（本 BSP 在
`hal_syscfg.h` 中映射到 `hal_syscfg_usb_phy0_sw_host()`）切换角色。

公共组件中有两处通用修复。其一是 `usb_hc_ehci.c` 的
`ehci_control_urb_init()`：
在填充 SETUP qTD 后调用 `usb_dcache_clean()`。旧版 EHCI core 只维护传输
缓冲区的缓存，SETUP 包位于普通缓存内存、由控制器 DMA 读取，在 C906 这
类非一致性平台上可能读到旧数据（表现为 SET_ADDRESS 阶段 BABBLE）；新版
原厂 CherryUSB 已在 `ehci_qtd_fill()` 中对每个 qTD 缓冲区统一做缓存
维护，因此该修复按同样的通用方式放在 core 中，而不是板级特判。

### 配置

```text
CONFIG_BSP_USING_USB1_HOST=y            # 或 BSP_USING_USB0_HOST
CONFIG_RT_USING_CHERRYUSB=y
CONFIG_RT_CHERRYUSB_HOST=y
CONFIG_RT_CHERRYUSB_HOST_EHCI_AIC=y     # 必须显式选择（choice 不能被 select）
CONFIG_RT_CHERRYUSB_HOST_MSC=y
CONFIG_RT_USING_DFS=y
CONFIG_RT_USING_DFS_ELMFAT=y
CONFIG_RT_USING_CACHE=y                 # CherryUSB 的 RISC-V 检查要求
CONFIG_RT_USING_TIMER_SOFT=y            # 主机栈的定时器要求
```

`BSP_USING_USB0_HOST`/`BSP_USING_USB1_HOST` 会 select CherryUSB 主机栈、
MSC、DFS/ELMFAT、`RT_USING_CACHE` 和软件定时器。Kconfig 不能 select
choice 符号，所以 `RT_CHERRYUSB_HOST_EHCI_AIC` 必须在 `.config` 中显式
设置；`configs/d213-usb-utest.config` 已包含完整组合（当前选 USB1，
因为 USB0 留给 BROM/aicupg 刷写）。

原厂 AIC glue 用 `KERNEL_RTTHREAD` 选择 RT-Thread 集成路径，这个符号
只存在于 Luban-Lite 的 Kconfig；上游 RT-Thread 没有，因此本 BSP 在
`../libraries/soc/d21x/include/aic_compat.h` 中提供它，否则 `usbh_initialize()` 不会被
`INIT_ENV_EXPORT` 调用，主机线程也不会创建。

### 数据通路

```text
应用 read/write / DFS
  → /dev/sdN 块设备（rt_usbh_msc.c）
  → SCSI READ10/WRITE10
  → USB Bulk IN/OUT（EHCI QH/QTD）
  → 所选端口 PHY（UTMI）→ U 盘
```

EHCI/OHCI 描述符位于普通 DDR，通过 `usb_dcache_clean/invalidate/flush`
钩子维护 C906 D-Cache；这些钩子在 `../libraries/soc/d21x/drivers/drv_usb.c` 中实现。
`usb_dcache_invalidate()` 采用 clean+invalidate，避免按 64 字节行失效
时丢弃相邻的脏数据。

### 使用

```text
msh /> lsusb
msh /> ls /          # U 盘 FAT 自动挂载到 "/"
```

U 盘枚举成功后 `rt_usbh_msc.c` 注册 `/dev/sda` 并自动挂载
`CONFIG_USB_DFS_MOUNT_POINT`（默认 `/`）。挂载不会格式化或写入磁盘。

### USB Device（CDC ACM）

当前候选不修改公共 CherryUSB port。官方 AIC 更新 `a0054795c440` 已完成
bus-id API 等适配，但本基线的 RT-Thread 内置版本尚未同步；以下两个 Device
配置暂时依赖这次公共更新。旧硬件报告基于已撤销的本地 port 修改，不能用来
宣称当前候选通过。USB Host 不依赖该 Device port。详见 21.5。

USB0 可切换为高速设备控制器，当前只暴露一个 CDC ACM 功能。配置：

```text
CONFIG_BSP_USING_USB0_DEVICE=y
CONFIG_RT_USING_CHERRYUSB=y
CONFIG_RT_CHERRYUSB_DEVICE=y
CONFIG_RT_CHERRYUSB_DEVICE_SPEED_HS=y       # choice，必须显式选择
CONFIG_RT_CHERRYUSB_DEVICE_AIC=y            # choice，必须显式选择
CONFIG_RT_CHERRYUSB_DEVICE_CDC_ACM=y
CONFIG_RT_CHERRYUSB_DEVICE_CDC_ACM_CHARDEV=y
```

`BSP_USING_USB0_DEVICE` 与 `BSP_USING_USB0_HOST` 互斥。Kconfig 不能
select choice 符号，所以 UDC IP（`aic`）和速率（`HS`）必须在
CherryUSB 菜单或 `.config` 中显式选择；`configs/d213-usbdev-utest.config`
已包含完整组合。

`board/usb_device.c` 在 `INIT_APP_EXPORT` 中注册描述符（VID
`0x33C3`、PID `0x6688`）、调用 `usbd_cdc_acm_serial_init()` 并启动
`usbd_initialize()`。`rt_usbd_serial.c` 随后注册字符设备 `usb-acm0`。
数据通路：

```text
应用 read/write（usb-acm0）
  → rt_usbd_serial.c 环形缓冲
  → CDC Bulk IN/OUT（USBDEV_CDC_IN_EP/OUT_EP）
  → AIC UDC FIFO（PIO）
  → USB0 PHY → PC（CDC ACM）
```

低层初始化（PHY 角色、`CLK_USBD`/`CLK_USB_PHY0`、`RESET_USBD`/
`RESET_USBPHY0`、中断 34）由原厂 `port/aic/usb_dc_aic_ll.c` 完成；
本 BSP 在 `aic_compat.h` 中补齐 `USB_DEV_BASE`、`USB_DEV_IRQn`、
`AIC_USING_USB0_DEVICE` 和 `csi_dcache_clean_range()` 映射，并在
`board/usb_config.h` 中提供 `CONFIG_USB_AIC_DC_*`。

公共组件的第二处通用修复：RT-Thread 组件自带的 CherryUSB 1.6.1
`port/aic` 仍按旧版 API 调用设备核心（缺少 `busid` 参数、IRQ 处理
函数签名不一致），且没有包含 AIC 头文件，无法与同组件内的
`usbd_core` 编译通过。本 BSP 对 `port/aic/usb_dc_aic.c` 和
`usb_dc_aic_ll.c` 做了最小 API 对齐（补充 `busid`、`USBD_IRQHandler(uint8_t)`、
ISR 包装、`aic_core.h` 包含），没有改动核心或类驱动。

同一端口文件还修正了 `usbd_ep_start_read_raw()` 的 OUT 端点等待
守卫：循环上限是 100000，但超时判断写成了 `i == 1000000`，守卫永远
不生效，端点已使能时仍会继续改写 XFRSIZ/EPENA。`usb_dev_console on`
会先由 finsh 打开 `usb-acm0`，再由 console 以不同 flag 打开一次，
第二次 open 正好命中该路径。修正后与原厂 SDK 行为一致：等待超时
直接返回 `-2`，不再触碰已使能的端点。

使用：

```text
msh /> usb_dev_status          # usb-acm0 是否注册、是否已配置
msh /> usb_dev_console on      # 把 MSH 和 console 切到 usb-acm0
msh /> usb_dev_console off     # 切回 uart0
```

`usb_dev_console on` 仅在 `usb-acm0` 已注册且 USB 已完成枚举
（`usb_device_is_configured(0)`）时执行，避免 `usbd_serial_open()`
无主机时永久等待；如果拔掉 USB 线，控制台不会自动切回。
`usb_dev_console off` 先切 console 再切 shell：serial v1 的
`rt_serial_open()` 会用最后一次 open 的 flag 覆盖 `open_flag`，console
的 open 不带 `INT_RX`，必须排在 finsh 的 open 之前，uart0 才能保住
RX FIFO 和 `INT_RX`。该顺序与 UDC 端点守卫修正一起，连续 16 轮
on/off 实机通过。

验证状态：2026-11-09 实机。`d213-usbdev-utest` 构建、
ELF/静态检查、打包和 `bsp.d213.usbdev` 均 PASS（HIL
`20261109-053213-7196`，`_utest_testcase=12`）。USB0 接 PC 后 Windows
枚举出 VID `0x33C3`/PID `0x6688` 复合设备和 CDC ACM 串口；
`usb_dev_status` 返回 `configured=1`；`usb_dev_console on` 后可在 CDC
串口上双向执行 MSH 命令并收到回显，`off` 后 UART 恢复输入输出。
`d213_usb_console.py` 连续 16 轮 on/off 全 PASS（报告
`20261109-053318`、`20261109-053332`、`20261109-053348`）。

### 限制与证据等级

- USB0/USB1 都作为 Host 接入，但同一镜像只启用一个（`MAX_BUS=1`）；
  实机验证的是 USB1（U 盘 MSC）；USB0 Host 未在实机上回归，因为
  USB0 被 BROM/aicupg 刷写占用；
- USB0 Device 模式（CDC ACM）已实机验证枚举、双向数据和
  `usb_dev_console on/off` 切换；切换时端口可能打印
  `[E/USB] ep2 ... 0x80088200`，这是端点已被上一次 open 使能时的
  保护路径，不是错误状态。OTG 自动角色切换和其他设备类没有接入；
- 高速设备走 EHCI；低速/全速设备依赖 OHCI 伴随控制器，当前未做实机
  验证（`usb_status` 可同时打印 EHCI、OHCI 根端口状态）；
- 没有 VBUS 控制/过流检测；热插拔由 CherryUSB hub 线程处理；
- MSC 只做块设备与 FAT 挂载，没有文件写压力测试；`bsp.d213.usb`
  只读磁盘；
- 2026-11-08 实机：HIL `20261108-074359-19904`
  （`d213-usb-utest`）`bsp.d213.usb` PASS——USB1 枚举、MSC、`sda`
  注册、GETGEOME、两次 8 扇区读一致、MBR `0x55AA` 签名全部通过。
  详细记录见 [22-utest-regression.md](22-utest-regression.md) 的
  `bsp.d213.usb` 条目。

## 14.3 SDMC / SDIO / eMMC

### 硬件

D21x 有 3 个 SD/eMMC/SDIO 控制器（SDMC0/1/2）。demo128 有 SD 卡槽。启动顺序中 SDMC1 用于 SD 启动，SDMC0 用于 eMMC。

### 原厂代码

```text
bsp/artinchip/hal/sdmc/
bsp/artinchip/drv/sdmc/
```

### 接入依赖

- SDMC 时钟/复位；
- 4/8-bit 总线；
- DMA；
- 卡检测/电源；
- RT-Thread SDIO/MMCSD 框架；
- 文件系统。

### 当前结论

SDMC1（SD 卡）和 SDMC2（SDIO）都已接入标准 RT-Thread MMCSD/SDIO 框架。
SDMC1 的块设备、只读 utest、FAT 挂载和 IDMA 回归已 HW PASS；SDMC2 的
SDIO 主机路径配合 RTL8733BS 模组验证，见 `12-storage.md` 12.15。eMMC
（SDMC0）和 SDIO Wi-Fi 协议栈仍未实现。

## 14.4 Ethernet

### 硬件

D21x 有 GMAC 控制器（按型号 1-2 个）。D213ECV-DEMO-V4-0 使用 GMAC0
RMII 接 RTL8201F，PHY 地址由 LED0/LED1 上拉电阻决定为 1：

| 信号 | SoC 引脚 | 功能 | 说明 |
|---|---|---|---|
| RXD0/RXD1 | PE.0/PE.1 | 6 | RMII 接收 |
| CRS_DV | PE.2 | 6 | RMII 载波/数据有效 |
| REF_CLK | PE.3 | 6 | PHY 输出 50 MHz |
| TXD1/TXD0 | PE.4/PE.5 | 6 | RMII 发送 |
| PHY_RST | PE.6 | 1（GPIO） | 低有效复位 |
| TX_EN | PE.7 | 6 | RMII 发送使能 |
| MDC/MDIO | PE.8/PE.9 | 6 | PHY 管理接口 |
| EPHY0-25M | PE.10 | 6（CLK_OUT2） | 25 MHz 时钟到 PHY XTAL_IN |

**PE.10 的 25 MHz 是必须的。** RTL8201F 没有独立晶振，XTAL_IN 由
CLK_OUT2 提供；该时钟缺失时 PHY 不上电工作，MDIO 在所有地址都读回
`0xFFFF`（本次 bring-up 的实际现象）。`drv_mac.c` 在释放 PHY 复位前
调用 `hal_clk_set_freq(CLK_OUT2, 25000000)` 和 `hal_clk_enable(CLK_OUT2)`，
`board/pinmux.c` 同时把 PE.10 配为功能 6。

### 实现

```text
../libraries/soc/d21x/hal/aic_mac.c、aic_mac_ll.c、aic_phy.c   原厂 GMAC/PHY HAL
../libraries/soc/d21x/include/aic_mac*.h、aic_phy.h
../libraries/soc/d21x/drivers/drv_mac.c                               eth_device + PHY 轮询线程
board/pinmux.c                                  GMAC0/EPHY 引脚
../libraries/soc/d21x/port/aic_hal_clk.c                       CLK_GMAC0、CLK_OUT2
../libraries/soc/d21x/port/aic_hal_syscfg.c                    RMII/外部 REF_CLK 选择
```

- 设备注册为 `e0`，走 RT-Thread 传统 `eth_device` + lwIP 2.1.2；
- MAC 地址默认 `02:00:00:00:00:01`，可由 `BSP_GMAC0_MACADDR` 覆盖；
- PHY 地址 `BSP_GMAC0_PHYADDR`（默认 1），复位 GPIO
  `BSP_GMAC0_PHYRST_GPIO`（默认 `PE.6`，置空则跳过）；
- 2 s 轮询 PHY，链路变化时配置 100M/全双工/流控并启停 MAC；
- RX 完成中断唤醒 lwIP，TX/RX 均做 D-Cache 维护；
- DHCP 默认开启，12 s 内取得租约。

### 构建与验证

```sh
python bsp/artinchip/tools/build-check.py \
    --targets d213-eth-utest --sdk /path/to/luban-lite
python bsp/artinchip/tools/d213_hil.py --mode full \
    --confirm-board d213-demo128 --target d213-eth-utest \
    --suite bsp.d213.eth --sdk /path/to/luban-lite
```

2026-11-08 实机（网线接路由器）：

- HIL `20261108-084817-25812`、`20261108-085007-17888` 均 PASS；
- `bsp.d213.eth` 在 10 s 内链路 UP，DHCP 取得 `192.168.31.61/24`，
  网关/DNS `192.168.31.1`；
- `ping 192.168.31.1` 4/4 回包，0% 丢包，RTT 0–2 ms；
- PHY ID `0x001c:c816`（RTL8201F），自协商结果 100M 全双工；
- CLK_OUT2 寄存器读回 `0x0001002f`（gate + 48 分频 = 25 MHz）。

### 限制

- 只验证 GMAC0 RMII；RGMII、GMAC1 未实现；
- 无 PHY 中断和热插拔，链路状态靠轮询；
- 未做吞吐、长时间压力、休眠唤醒测试；
- 默认 `.config` 不启用 GMAC0，需在 menuconfig 中打开
  `BSP_USING_GMAC0` 并选择 lwIP 2.1.2。

## 14.5 Wi-Fi 与蓝牙

D21x **没有原生 Wi-Fi/蓝牙控制器**。demo128 的 Wi-Fi 是外部模组，通常通过 SDIO 或 USB 连接。D213ECV-DEMO-V4-0 配 RTL8733BS（SDIO 2.0 4-bit Wi-Fi+BT combo，SDMC2 PF.0..PF.5，WL_REG_ON PD.1，BT UART7 PF.6..PF.9）。

因此：

- SDIO 主机驱动是前提；
- 模组驱动是第二层；
- WLAN/蓝牙协议栈是第三层；
- 固件加载、MAC 地址、天线和认证是第四层。

当前 BSP 完成第一层，以及 BT 侧的物理端口：

- SDMC2 标准 SDIO 主机（`bsp.d213.sdio` 用例）：枚举与 CMD52 已实机
  通过，CMD53 数据阶段仍失败，见 `12-storage.md` 12.15；
- UART7 作为 BT HCI 端口（`BSP_USING_UART7`，可选 `BSP_USING_UART7_AUTO_FLOW_CTRL`
  硬件 RTS/CTS），套件 `bsp.d213.uart7` 用内部回环验证，见
  `07-uart.md` 7.11。

模组固件、Realtek 驱动和 WLAN/BT 协议栈不在本 BSP 内。不能把“板上有
Wi-Fi 模组”写成“支持 Wi-Fi”。

## 14.6 音频

### 硬件

D21x 的音频控制器是独立的 DSPK/DMIC Codec，不是 I2S 控制器。D213ECV-DEMO-V4-0 的接法：

| 信号 | 引脚 | 功能 | 说明 |
|---|---|---|---|
| SPK1 | PE.11 | 4 | PWM/DSPK 输出到板载扬声器功放 |
| PA_SHDN | PA.7 | 1（GPIO） | 功放关断，低有效；高电平为静音 |
| DMIC_CLK | PE.12 | 4 | 数字麦克风位时钟（Codec 输出） |
| DMIC_D0 | PE.13 | 4 | 数字麦克风数据（Codec 输入） |

板上两颗 MEMS 麦克风（U9/U12）经 100R 电阻接到同一组 DMIC_CLK/DMIC_D0；
驱动支持 1/2 声道配置。DMIC 的时钟和数据都由 Codec 提供，没有独立的
I2S 控制器参与。

### 原厂代码与 BSP 组织

```text
../libraries/soc/d21x/hal/hal_audio.c          原厂 Codec HAL（保留版权）
../libraries/soc/d21x/include/hal_audio.h
../libraries/soc/d21x/include/hal_audio_reg.h
../libraries/soc/d21x/drivers/drv_audio.c             RT-Thread 音频设备 "sound0"（播放）
../libraries/soc/d21x/drivers/drv_dmic.c              RT-Thread 音频设备 "dmic0"（DMIC 采集）
../libraries/soc/d21x/port/aic_hal_clk.c       PLL_FRA1 分数分频与 Codec 分频
board/pinmux.c                  PE.11 / PA.7 / PE.12 / PE.13
```

与 UART、SDMC 一样，这里复用原厂寄存器序列，不引入原厂 `drv/audio`
的 PM/OSAL 依赖。音频 HAL 需要的 DMA API（`hal_request_dma_chan`、
`hal_dma_chan_prep_cyclic` 等）由已移植的 `../libraries/soc/d21x/hal/hal_dma.c`
提供。

### 时钟

Codec 模块需要 24.576 MHz（48/32/24/16/12/8 kHz 家族）或
22.5792 MHz（44.1/22.05/11.025 kHz 家族）。原厂做法是把 PLL_FRA1
设为模块时钟 × 20，再在 `CLK_CODEC_REG`（0x0830）里除以 20：

```text
CLK_CODEC  = PLL_FRA1 / CLK_CODEC_REG[4:0]
PLL_FRA1   = module_freq × 20
```

`../libraries/soc/d21x/port/aic_hal_clk.c` 只为 PLL_FRA1 实现了分数分频序列（整数
因子 + 0x1ffff 小数部分 + SDM 固定模式），因为本 BSP 没有其他外设
使用 PLL_FRA1（I2S0/I2S1 未启用）。

序列必须严格按原厂顺序：先把 `PLL_OUT_MUX` 切到 24 MHz 旁路，再编程
整数/小数因子和 SDM，置 `PLL_EN`，`aic_udelay(200)` 等待锁定，最后把
`PLL_OUT_MUX` 切回 PLL 输出。漏掉最后一步时，PLL 本身已经锁定，但
Codec 模块时钟停在 24 MHz ÷ 20 = 1.2 MHz，所有音频速率的实际采样率
约为标称值的 1/20（播放和采集的周期中断都会极慢）。调试音频时先读
`PLL_FRA1_GEN_REG` 的 bit 20（`PLL_OUT_MUX`）确认输出选择。

### 数据通路

```text
应用 rt_device_write()
  → RT-Thread audio replay 队列（4 KiB 块）
  → 驱动 8 KiB 环形缓冲（2 × 4 KiB 周期，64 B 对齐）
  → DMAC 循环传输（DRQ 14，DMA_ID_AUDIO_DMIC）
  → Codec TX FIFO → DSPK/PWM → PE.11 → 功放 → 扬声器
```

每个 DMA 周期完成中断调用 `rt_audio_tx_complete()`；框架把下一块
数据拷进环形缓冲后调用驱动的 `transmit()`，驱动在那里对刚写入的
周期做 `aic_dma_cache_clean()`。C906 的 D-Cache 不是一致的，这一步
不能省。

采集方向：

```text
DMIC（PE.12/PE.13）
  → Codec RX 抽取滤波器（按采样率配置 FS）
  → DMIC RX FIFO（DRQ 14，DMA_ID_AUDIO_DMIC）
  → DMAC 循环传输（2 B/样本单声道，4 B/样本双声道）
  → 驱动周期中断 rt_audio_rx_done()
  → RT-Thread audio record pipe（2 KiB）
  → 应用 rt_device_read()
```

DMIC 周期中断在把完成周期交给 record pipe 之前对整个 4 KiB 环形缓冲
做 `aic_dma_cache_invalidate()`（D-Cache 不是一致的，DMAC 直接写
内存），随后调用 `rt_audio_rx_done()`。record pipe 满时框架覆盖最旧
数据，因此 `rt_device_read()` 应按周期持续排空。

### 配置和使用

```text
CONFIG_BSP_USING_AUDIO=y
CONFIG_BSP_AUDIO_SPK1=y
CONFIG_BSP_AUDIO_PA_GPIO="PA.7"

CONFIG_BSP_USING_DMIC=y      # 可选：DMIC 采集，dmic0
```

`BSP_USING_AUDIO` 会 select `RT_USING_AUDIO`、`RT_USING_MEMPOOL`、
`BSP_USING_DMAC` 和 `BSP_USING_GPIO`；`BSP_USING_DMIC` 会 select
`RT_USING_AUDIO` 和 `BSP_USING_DMAC`，两者可以独立打开。测试镜像：
`configs/d213-audio-utest.config`（播放和 DMIC 采集都打开）。

```c
rt_device_t dev = rt_device_find("sound0");
struct rt_audio_caps caps = {0};

rt_device_open(dev, RT_DEVICE_OFLAG_WRONLY);

caps.main_type = AUDIO_TYPE_OUTPUT;
caps.sub_type = AUDIO_DSP_PARAM;
caps.udata.config.samplerate = 48000;
caps.udata.config.channels = 2;
caps.udata.config.samplebits = 16;
rt_device_control(dev, AUDIO_CTL_CONFIGURE, &caps);

rt_device_write(dev, 0, pcm, len);   /* PCM 16-bit little-endian */
rt_device_close(dev);                /* 等待剩余数据播完并关闭功放 */
```

采集设备 `dmic0` 是标准只读 audio 设备，配置走 `AUDIO_TYPE_INPUT`：

```c
rt_device_t dev = rt_device_find("dmic0");
struct rt_audio_caps caps = {0};

rt_device_open(dev, RT_DEVICE_OFLAG_RDONLY);

caps.main_type = AUDIO_TYPE_INPUT;
caps.sub_type = AUDIO_DSP_PARAM;
caps.udata.config.samplerate = 16000;
caps.udata.config.channels = 1;
caps.udata.config.samplebits = 16;
rt_device_control(dev, AUDIO_CTL_CONFIGURE, &caps);

rt_device_read(dev, 0, pcm, 2048);   /* 按周期排空 record pipe */
rt_device_close(dev);                /* 停止 DMA 和抽取滤波器 */
```

手动验证命令：

```text
msh > audio_tone            # 1 kHz，1 秒
msh > audio_tone 440 2000   # 440 Hz，2 秒
msh > dmic_record           # 16 kHz/单声道采集 1 秒并打印电平
msh > dmic_record 3000      # 采集 3 秒
```

### 限制与证据等级

- 播放（`sound0`）和 DMIC 采集（`dmic0`）都已接入；AMIC 采集、I2S、
  音量渐变之外的音效没有接入；
- 两个方向都只支持 16-bit 样本，采样率限于
  8/11.025/12/16/22.05/24/32/44.1/48 kHz；采集支持 1/2 声道；
- 功放只在播放期间打开，`close()` 后 PA_SHDN 拉高；
- 播放与采集共享 Codec 模块时钟，同一时刻只能有一个流：后启动的流
  会先停掉另一条流（框架忽略 `start()` 错误，驱动因此选择接管而不是
  拒绝）；运行中修改采样率/声道返回 `-RT_EBUSY`；
- `bsp.d213.audio` 用例验证设备、格式协商、DMA 周期推进和关闭，
  **不能**证明扬声器真的有声音（需要人耳或麦克风）；
- `dmic0` 的默认采集音量是 0 dB（`AUDIO_MIXER_VOLUME` = 100）。音量
  寄存器的 160 对应 0 dB，60 相当于 −32 dB；`bsp.d213.dmic` 用例会把
  音量设成 60，所以跑完用例后手动测电平前应重新设回 100 或重新上电；
- 2026-11-08 实机（HIL `20261108-222242-3036`）：`bsp.d213.dmic`
  PASS；0 dB 下 `dmic_record 2000 1` 得到 min −11787 / max 7023 /
  RMS 455，`dmic_record 2000 2` 两个声道各有独立电平（RMS 451/667），
  DMA 周期数与时基一致（单声道 2 s 约 31 个 2 KiB 周期）；
- 2026-11-09 共享层重构后复测（HIL `20261109-093546-7576`）：
  `bsp.d213.audio`、`bsp.d213.dmic` 均 PASS，`tc_dmic` 为
  2048 bytes, min −73 / max 15, periods +1；
- 声学通路：播放 1 kHz 扬声器音时 `dmic0` 的 RMS 从约 400 升到约
  1150，证明两颗 MEMS 麦克风确实把声音转换成 PDM 数据流；
- 引脚通路：把 PE13 切为 GPIO 后拉低/拉高数据脚，抽取器直流输出
  从 +80 变到 −77（HPF 关闭），证明 PE13 确实连到 DMIC 抽取器输入；
- 采样率与麦克风时钟：PDM 位时钟随采样率成比例变化（16 kHz 时约
  2 MHz，32 kHz 时约翻倍），32 kHz 以上会超出板上 MEMS 麦克风的
  时钟范围（实测 32 kHz 噪声明显变大），建议使用 8–24 kHz；
- 2026-11-08 实机（HIL `20261108-064519-15188`）：`d213-audio-utest`
  烧录启动后 `bsp.d213.audio` 的 `tc_audio_playback` PASS；用例打开
  `sound0`、按 48 kHz/2 ch/16-bit 配置并写入 8 KiB，DMA 周期计数推进，
  关闭排空正常；随后手动执行 `audio_tone 1000 2000` 返回 `msh >`，
  无错误输出；
- 证据等级：BUILD + STATIC + PACKAGE + HW（PLL_FRA1、Codec、DRQ、
  循环 DMAC、PA 控制、DMIC 声学采集）。扬声器声学输出仍需人耳确认，
  不能用编译通过代替。

## 14.7 视频与摄像头

### 硬件

D21x 的视频能力按型号不同。D213 可能具备 VE/GE/DE 等模块，但具体解码能力、分辨率和格式必须以数据手册为准。

### 原厂代码

```text
bsp/artinchip/hal/ve/
bsp/artinchip/drv/ve/
bsp/artinchip/hal/dvp/
bsp/artinchip/drv/dvp/
```

### 重要限制

- 不要把“有 VE”写成“支持 H.264/H.265 硬件解码”；
- 不要把“有 GE”写成“支持 2D/3D GPU”；
- 不要把“有 DVP”写成“摄像头已可用”；
- 不要推断 D211 或其他型号的视频能力与 D213 相同。

D213 VE v1.0 已加入 `BSP_USING_VE`（默认关闭），寄存器基址 `0x18c00000`、原始 PLIC IRQ 61、200 MHz 时钟。具体序列已对照 `https://github.com/boa-z/luban-lite` V1.3.3 快照 `4060f85bd27a1eb555952921139b77cc41bdeb4e` 复核。

通过标准 `ve` 设备 open/control/close，或 `aic_drv_ve.h` 的直接接口使用，不能混用两种 open 方式。单 opener 可供多个 codec context 共享，但每个事务 GET_CLIENT/WAIT/RESET/PUT_CLIENT 必须在同一线程执行；GET 防止递归获取并限制竞争等待，WAIT 的合法范围是 0–2000 ms。非 owner 操作被拒绝；未完成的事务 PUT 会复位停 DMA，成功 WAIT 后 PUT 保留已完成的硬件状态以兼容跨帧 Codec。若成功 WAIT 后又启动下一次 DMA 并失败，调用者必须显式 RESET 再 PUT。上下文生命周期和具体 Codec 仍由外部库负责。

IRQ 清除使用原厂 v1.0 AVC/JPEG/PNG status 寄存器布局，忽略 status=0 的重复 PLIC 投递，复位清除旧完成事件。WAIT 超时先复位再返回，close 不会在持有事务的线程中强行断电。设备 close 失败时保留 RT-Thread 引用，以便 PUT 后重试。驱动只允许可阻塞的线程上下文。没有引入 H.264/JPEG/PNG 算法源码。

DVP/VIN 本轮仅审查，**不正式合入采集驱动或 DVP 时钟配置**。MPP Patch 的 DVP HAL/driver 还涉及旧 OSAL、camera device、pinmux/24 MHz 参考时钟、传感器供电/复位和 VB queue；将 VB/消费者队列直接放入 BSP 会反向绑定媒体包。后续应先确定实际 sensor 与总线极性，再在 BSP 提供独立 DVP 硬件接口，把 VB 和媒体组件留在上层。未验证时不应把 `AIC_USING_CAMERA` 宏当成已具备传感器驱动。

本轮 VE/GE/DE 只完成构建、主机测试与接口审查，硬件状态仍为待上板。显示所有权与 Buffer 交接见第 13.18 节。

## 14.8 ADC / 工业外设

D21x 提供 PSADC/GPADC、EPWM、CAP、QEP、CIR 等工业相关外设。原厂代码位于：

```text
bsp/artinchip/hal/psadc/
bsp/artinchip/hal/gpai/
bsp/artinchip/hal/epwm/
bsp/artinchip/hal/inputcap/
bsp/artinchip/hal/qep/
bsp/artinchip/hal/cir/
```

当前均未接入。接入前必须确认：

- 具体型号是否有该实例；
- 板卡是否引出；
- 通道数/引脚；
- DMA/中断；
- 校准/参考电压。

## 14.9 电源管理与休眠

原厂有 `bsp/artinchip/drv/pm/` 和 RTC 唤醒相关代码。本 BSP 没有实现：

- suspend/resume；
- 深睡；
- 唤醒源；
- 外设状态保存/恢复；
- 功耗测量。

在没有验证唤醒路径前，不应在产品中使用休眠。

## 14.10 接入优先级建议

如果目标是工业控制终端，建议顺序：

1. 存储（NAND/MTD/文件系统）；
2. Ethernet——GMAC0 + RTL8201F 已实现并实机验证（DHCP + ping），
   剩余吞吐/长时间稳定性测试；
3. USB Host（U 盘/升级）——Host + MSC/FAT 已实现并实机验证，
   剩余升级流程与 Device/OTG 模式；
4. SDIO Wi-Fi（如产品需要）；
5. LVGL 9.6.0 显示/触摸实机校准（GT911 已接入，见 13）；
6. 音频；
7. 视频/摄像头；
8. 低功耗。

如果目标是图形仪表，建议顺序：

1. 面板实机点亮（LVDS/DE/framebuffer 已实现，待验证）；
2. GE Normal 实机验证；
3. LVGL 9.6.0 实机验证；
4. 触摸输入；
5. 存储/网络。

无论顺序如何，每一步都必须有实机验证和文档更新，不能只写代码就标记为支持。
