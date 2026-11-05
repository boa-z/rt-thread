# 14 连接、多媒体与扩展外设

## 14.1 当前状态

本 BSP 当前只实现了 GPIO、UART、I2C、CAN、PWM、WDT、RTC。

以下功能**未实现（N/I）**：

- USB Host/Device；
- SDMC/SDIO/eMMC；
- Ethernet；
- Wi-Fi/蓝牙；
- I2S/音频；
- DVP 摄像头；
- 视频引擎（VE）；
- PSADC/GPADC；
- EPWM/CAP/QEP；
- CIR；
- eFuse/SID；
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

即 demo128 默认使用 USB0 Host。

### 原厂代码

```text
bsp/artinchip/drv/usb/          （或 SDK 中 USB 相关目录）
packages/third-party/cherryusb/  （部分 SDK 版本）
```

### 接入依赖

- USB PHY/时钟/复位；
- 角色（Host/Device/OTG）；
- VBUS 检测/控制；
- 中断；
- 缓存一致性（USB DMA）；
- RT-Thread USB Host/Device 栈或 CherryUSB。

### 当前结论

N/I。不能因为原厂支持 USB Host 就声称本 BSP 支持 U 盘、键盘或 CDC。

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

N/I。没有实现 SD 卡挂载，也没有实现 SDIO Wi-Fi 主机控制器。

## 14.4 Ethernet

### 硬件

D21x 有 GMAC 控制器（按型号 1-2 个）。demo128 为 100M Ethernet。

### 原厂代码

```text
bsp/artinchip/hal/mac/
bsp/artinchip/drv/mac/
```

### 接入依赖

- MAC 时钟/复位；
- MDIO/PHY；
- DMA 描述符；
- 中断；
- RT-Thread netdev + LwIP；
- PHY 时序/复位 GPIO。

### 当前结论

N/I。存在 MAC 控制器不等于板卡以太网可用；还需要 PHY、变压器、RJ45 和正确的板级配置。

## 14.5 Wi-Fi 与蓝牙

D21x **没有原生 Wi-Fi/蓝牙控制器**。demo128 的 Wi-Fi 是外部模组，通常通过 SDIO 或 USB 连接。

因此：

- SDIO 主机驱动是前提；
- 模组驱动是第二层；
- WLAN/蓝牙协议栈是第三层；
- 固件加载、MAC 地址、天线和认证是第四层。

当前 BSP 四层都没有实现。不能把“板上有 Wi-Fi 模组”写成“支持 Wi-Fi”。

## 14.6 音频

### 硬件

D21x 有 I2S/Codec 相关控制器。demo128 有 DSPK/DMIC 和外部音频使能引脚。

### 原厂代码

```text
bsp/artinchip/hal/i2s/
bsp/artinchip/hal/audio/
bsp/artinchip/drv/audio/
bsp/artinchip/drv/i2s/
```

### 接入依赖

- I2S 时钟；
- DMA；
- Codec 配置（I2C 或寄存器）；
- 采样率/位宽/通道；
- 音频使能 GPIO；
- RT-Thread audio 框架或自定义设备。

当前 N/I。

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

当前 N/I。

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
2. Ethernet；
3. USB Host（U 盘/升级）；
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
