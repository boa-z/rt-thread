# D126FxR-DEMO-V1-1 板卡对应关系

## 1. 官方结论

官方页面：

<https://aicdoc.artinchip.com/topics/quickstart/d126-boards-d12xpro.html>

页面 URL 锚点为 `d126fxv-demo-v2-0`，容易让人误以为对应
`d126fxv-demo-v2-0`。实际页面中 **D126FxR-DEMO-V1-1** 的开发板条目
指向：

```text
Luban-Lite target: target/d12p/d126-demo124-nor
```

因此：

- 本 BSP 使用 `d12p` SoC 支持；
- 板级配置以 `target/d12p/d126-demo124-nor` 为参考；
- 不把 `d126fxv-demo-v2-0` 或 `d126-demo124-mmc` 的参数当作本板参数。

## 2. 官方板级信息与确认状态

| 项目 | 官方页面/原厂配置 | 状态 | 说明 |
|---|---|---|---|
| 板卡 | D126FxR-DEMO-V1-1 | 已确认 | 页面条目 |
| SDK target | d126-demo124-nor | 已确认 | 官方页面链接 |
| 封装 | BGA124 | 已确认 | 页面 |
| 启动介质 | SPI NOR 16 MB | 已确认 | 页面 + `image_cfg.json` |
| PSRAM | 8 / 16 MB | 待实物 | PBP 有多套配置 |
| 显示接口 | MIPI DSI | 已确认 | 4 lane + clock：PD18..PD27；背光 `LCD_PWM=PF.0`（原理图 page 8） |
| 屏幕 | 原厂默认 EK79007AD 1024x600 | 待实物 | 原理图只给出 32 pin FPC 和 1024x600 MIPI；panel 型号/时序仍需确认 |
| 触摸 | 参考同系列配置：GT911，I2C0，0x5d | 部分确认 | 原理图确认 I2C0 `SCL=PD.0`/`SDA=PD.1`，`CTP_RST=PA.10`，`CTP_INT=PA.11`；IC 型号/地址待实物 |
| 调试串口 | UART0，PA0/PA1，115200 | 已确认 | 原理图与实机 HW PASS（2026-11-10） |
| RTC | 32.768 kHz 晶振 + 电池备份 | 原理图确认 | Y2 接 PS2/RTC_XI、PS3/RTC_XO，RTC_VCOIN 接 J9 电池座；驱动未实现 |
| 下载/调试 | USB / JTAG | 部分确认 | PA.10/PA.11 同时是 JTAG_MS/CK 与 CTP_RST/INT，复用不能同时使用 |
| Wi-Fi | SDIO AIC8800 | 页面 | 未实现 |
| TF 卡 | SDIO | 页面 | 未实现 |
| CAN | CAN FD | 页面 | 未实现 |
| RS485/UART | 多路 | 页面 | 仅 UART0 控制台 |

## 3. 原厂 D126 target 关键参数

来自：

```text
target/d12p/d126-demo124-nor/pack/image_cfg.json
target/d12p/d126-demo124-nor/pack/pbp_cfg.json
target/configs/d12p_d126-demo124-nor_rt-thread_helloworld_defconfig
```

- SPI NOR 16 MB；
- 分区：`spl 512k`、`env 128k`、`env_r 128k`、`userid 256k`、
  `os 4m`、`rodata 9m`、`data 1m`；
- PBP 默认 PSRAM 配置 `cfg0` 为 XCCELA AP12816*2 16 MB；
- 还有 `cfg9` APS6408L 8 MB、`cfg2` APS3208K 8 MB、`cfg1` 32 MB 等
  多套配置；
- 原厂默认 PSRAM 初始化由 PBP 根据 fuse/ID 选择，不应在应用层修改；
- RT-Thread defconfig 使用 `CONFIG_AIC_PSRAM_SW_SIZE=0x200000`。

**重要：** `cfg0` 的注释写着 `D126FCV/default`，但这不能证明
D126FxR-DEMO-V1-1 实物一定贴 16 MB PSRAM。本 BSP 默认按 8 MB
保守使用。

## 4. 尚未确认、不得当作已验证的项目

以下项目必须通过实物、原理图或原厂确认后才能标为 HW：

1. 具体 D126 料号（D126FCV / D126FBV / 其他）和 PCB 修订；
2. PSRAM 实际型号、容量、位宽和 PBP 选择结果；
3. SPI NOR 实际型号、页/块大小和时序；
4. UART0 实际引脚、电平、波特率和 PLL 频率（引脚/波特率已由原理图
   和实机确认，PLL 由 `clock.c` 推导）；
5. MIPI DSI 通道数、屏幕型号、分辨率、时序、复位/背光（lane 和
   背光引脚已确认，panel 型号/时序/初始化序列未确认）；
6. 触摸 IC、I2C 实例、地址、复位/中断引脚（总线、RST/INT 引脚已由
   原理图确认，IC 型号、地址、有效电平和方向未确认）；
7. RTC 晶振实际频率误差、VCOIN 电池电压和掉电保持时间；
8. JTAG/USB 下载接口和量产工具版本；
9. Wi-Fi/TF/CAN/RS485 的实际连接。

## 5. 建议的实机确认方法

1. 用原厂 SDK 构建并启动 `d12p_d126-demo124-nor_rt-thread_helloworld`
   或 bootloader，记录完整启动日志；
2. 从 PBP 日志记录 PSRAM ID、容量、时钟和 PLL 频率；
3. 用万用表/示波器确认 UART0 PA0/PA1 和波特率；
4. 对照原理图确认屏幕 FPC、复位、背光、MIPI 通道；
5. 对照原理图确认 GT911 的 SCL/SDA/RST/INT；
6. 记录以上结果后，再更新 `board/pinmux`、`clock.c` 和显示参数。
