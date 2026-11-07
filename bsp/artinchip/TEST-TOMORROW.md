# 明天实机测试指南

本文按“先最小系统、后外设、最后显示/触摸”的顺序执行。
**BUILD/PACKAGE PASS 不等于 HW PASS**，所有实机结果请记录。

## 0. 共同准备

### 0.1 工具和恢复

- D213 使用现有 ArtInChip 升级/烧录工具和 USB 线；不要更换未经验证的工具。
- D126 必须先确认原厂 Bootloader、USB 升级方式和恢复流程。
- 烧录前备份原厂固件、分区配置和 bootloader。
- 不要擦除 `userid`、eFuse、安全启动状态。
- 不要烧录带 `PLACEHOLDER_DO_NOT_FLASH` 的镜像。

### 0.2 串口

```text
UART0, 115200 8N1
D213: PA.0/PA.1
D126: PA0/PA1（待实物确认）
```

### 0.3 构建/产物

源码仓库：

```text
https://github.com/boa-z/rt-thread
branch: bsp/artinchip-next-riscv
```

产物目录：

```text
bsp/artinchip/artifacts/d213-minimal/
bsp/artinchip/artifacts/d213-default/
bsp/artinchip/artifacts/d126-minimal/
bsp/artinchip/artifacts/manifest.md
bsp/artinchip/artifacts/SHA256SUMS
```

SHA-256 以 `bsp/artinchip/artifacts/manifest.md` 和 `SHA256SUMS` 为准；每次重新构建
哈希都会变化。

## 1. D213：恢复能力（先做）

1. 用原厂固件和升级工具确认可以正常进入下载/升级模式。
2. 记录当前分区表、bootloader 版本和升级工具版本。
3. 确认新镜像只写 `spl` 和 `os`，不覆盖无关数据。
4. 如果无法恢复，停止后续步骤，不要烧录新镜像。

## 2. D213 Minimal

**推荐第一个测试固件**：

```text
bsp/artinchip/artifacts/d213-minimal/images/d21x_d213_demo128_page_2k_block_128k_v1.0.0.img
```

烧录前确认：

- 文件 SHA-256 与 `manifest.md` 一致；
- 是 `d213-minimal` 目录，不是 default；
- 使用原厂 D21x bootloader 资源（`mk_firmware.py` 已打包）。

### 2.1 预期启动日志

```text
ArtInChip D213 demo128 RT-Thread BSP
CPU: T-Head C906 (RV64GC), tick: 1000 Hz
 \ | /
- RT -     Thread Operating System
...
msh >
```

不要要求日志逐字一致；关键是能进入 `msh >` 且不反复重启。

### 2.2 最小自检

```text
msh > aic_selftest
msh > list_thread
msh > list_device
msh > free
```

PASS 判定：

- `aic_selftest` 打印 `counter=2000`、`worker0/1: fpu context OK`；
- `list_thread` 能看到 main/tshell/idle 等线程；
- `list_device` 能看到 `uart0`；
- `free` 有明显可用堆；
- 30 秒内不复位、不卡死。

FAIL 时记录完整串口日志，尤其是：

- 是否停在 common `_start` / `rt_hw_board_init`；
- 是否 `Unhandled IRQ`；
- 是否 `CPU exception` / `mcause/mepc`；
- 是否看门狗复位。

## 3. D213 基本 CPU/中断

```text
msh > aic_selftest
msh > periph_status
```

观察：

- tick 是否持续增长（`list_thread` 的 idle 计数变化）；
- UART RX 是否能回显 MSH 输入；
- `aic_selftest` 的 FPU 测试是否稳定；
- 多次执行 `aic_selftest` 不崩溃。

## 4. D213 Default

固件：

```text
bsp/artinchip/artifacts/d213-default/images/d21x_d213_demo128_page_2k_block_128k_v1.0.0.img
```

先重复第 2 节最小系统检查，再执行：

```text
msh > periph_status
msh > i2c_scan 3
msh > gt911_dump
msh > touch_test 10
msh > disp_info
msh > disp_clk
msh > disp_colorbar
msh > disp_test 10
msh > ge_test
```

PASS 判定：

- `i2c_scan 3` 能看到 GT911 地址 `0x5d`；
- `gt911_dump` 能读到 ID；
- `touch_test 10` 坐标随四角/中心变化；
- `disp_colorbar` 色条正常，无花屏/闪烁；
- `disp_test 10` 双缓冲/VSync 正常，underflow 为 0；
- `ge_test` FillRect/BitBlt 正常。

外部设备测试：

```text
msh > can_baud 0 500000
msh > can_mode 0 normal
msh > can_send 0
msh > can_dump 0
```

CAN 需要外部收发器、终端电阻和对端节点；没有条件时标注 SKIP，
不要判 FAIL。

PWM/WDT/RTC：

```text
msh > pwm_test
msh > rtc_test
msh > wdt_test
```

WDT 测试可能复位，放在最后执行，并确认可以恢复。

## 5. D213 LVGL（可选）

如果本轮构建了独立的 `lvgl-aic-standalone` 固件：

```text
msh > lvgl_demo
msh > lvgl_demo status
msh > lvgl_touch 10
msh > lvgl_demo bench 10
```

检查点击、拖动、坐标方向和 flush 时间。LVGL 不在 BSP 内，不作为
D213 最小系统 PASS 的必要条件。

## 6. D126：先确认硬件

在烧录任何 D126 镜像前，确认：

- 板卡是 D126FxR-DEMO-V1-1；
- PCB 修订和芯片料号；
- PSRAM 实际容量（8/16 MB）；
- SPI NOR 16 MB；
- UART0 引脚和电平；
- 原厂 Bootloader 和 USB 升级/恢复方式；
- 屏幕/触摸型号和引脚。

如果以上无法确认，只做原厂固件启动和串口记录，不烧录新 BSP。

## 7. D126 Minimal

只有拿到为 `d126-demo124-nor` 构建的真实 `bootloader.bin` 后，才
生成可烧录镜像：

```bash
cd bsp/artinchip/d126-demo124-nor
AIC_SDK_DIR=/path/to/luban-lite-jc \
python3 tools/mk_firmware.py \
  --elf rtthread.elf \
  --bootloader /path/to/bootloader.bin \
  --psram-size 8m \
  --out images
```

如果使用 `--allow-placeholder-bootloader`，输出文件带
`PLACEHOLDER_DO_NOT_FLASH`，**不要烧录**。

### 7.1 预期启动日志

```text
ArtInChip D126 (D12p) RT-Thread BSP
CPU: T-Head E907F (RV32IMafc), ... Hz
UART0: ... Hz, 115200 baud
PSRAM: 0x40000000, 8 MB
CLIC: ... IRQs, ... control bits
 \ | /
- RT -     Thread Operating System
...
msh >
```

如果 `CLIC: 0 IRQs` 或串口乱码，先停止外设测试，记录日志。

### 7.2 D126 自检

```text
msh > list_thread
msh > free
msh > d126_selftest
msh > d126_memtest 1024
msh > pin num PA.2
msh > pin mode PA.2 output
msh > pin write PA.2 high
msh > pin read PA.2
```

注意：PA0/PA1 是 UART0，不要拿来做 GPIO 测试。GPIO 引脚必须按实际
原理图选择；未确认时只做 `pin num`，不要驱动。

PASS 判定：

- 能进 MSH；
- `d126_selftest` 显示 tick 约 100、fpu OK；
- `d126_memtest 1024` PASS；
- 30 秒内不复位；
- 无 `CPU exception`、无持续 `Unhandled IRQ`。

## 8. 失败时反馈

请提供：

1. 完整串口日志（从上电到失败/复位）；
2. 烧录的文件名和 SHA-256；
3. 板卡型号、PCB 修订、PSRAM/NOR 型号；
4. 使用的升级工具和步骤；
5. 是否只烧 `spl`/`os`，是否保留原 bootloader；
6. 失败现象：无输出、乱码、卡死、异常、复位循环。

## 9. 回退

- D213：重新烧录备份的原厂镜像/分区，确认能回 MSH 或原厂系统。
- D126：重新烧录原厂 D126 镜像和匹配的 bootloader/PBP；不要使用
  其他 D12x 板卡的 bootloader。
- 如果 Bootloader 被破坏，按原厂 USB/BROM 恢复流程处理，不要继续
  尝试未知镜像。

## 10. 结果记录模板

| 测试 | 命令/条件 | 结果 | 日志/备注 |
|---|---|---|---|
| D213 恢复模式 | 原厂工具 | PASS/FAIL | |
| D213 Minimal 启动 | 串口 115200 | PASS/FAIL | |
| D213 aic_selftest | MSH | PASS/FAIL | |
| D213 Default 启动 | MSH | PASS/FAIL | |
| D213 GT911 | touch_test | PASS/FAIL/SKIP | |
| D213 显示 | disp_colorbar/disp_test | PASS/FAIL | |
| D213 GE | ge_test | PASS/FAIL | |
| D213 CAN | can_send/can_dump | PASS/FAIL/SKIP | |
| D126 板卡确认 | 实物/原理图 | PASS/FAIL | |
| D126 Minimal 启动 | 串口 115200 | PASS/FAIL | |
| D126 tick/FPU | d126_selftest | PASS/FAIL | |
| D126 PSRAM | d126_memtest | PASS/FAIL | |
| D126 GPIO | pin 命令 | PASS/FAIL/SKIP | |
| D126 CLIC 中断 | 日志/GPIO IRQ | PASS/FAIL | |
