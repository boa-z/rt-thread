# 17 测试与问题排查

## 17.1 测试状态说明

本仓库当前能提供的证据：

- 默认 `.config` 编译通过；
- 链接映射和符号检查；
- FIT/ITB/IMG 打包成功；
- LVDS 彩条、双缓冲、VSync、GE FillRect/BitBlt 实机日志；
- 目录重组前的 UART0/Tick/FPU 基础启动日志（见下）。

当前不能提供的证据：

- 目录重组后的 UART0 启动回归日志；
- GPIO/I2C/CAN/PWM/WDT/RTC 的实机波形/总线日志；
- GE 缩放/混合/旋转的最终实机结果；
- LVGL 实机上屏日志；
- 压力测试和错误注入结果。

任何“通过”都必须注明是编译、静态还是实机。

### 参考构建记录

默认配置（UART0/1、GPIO、I2C3、CAN0/1、PWM、WDT、RTC、LVDS/DE、双缓冲、GE），
Homebrew GCC 16.1.0、Newlib 4.6.0、Python 3.13、SCons 4.8.1：

| 配置 | text | data | bss | 合计 |
|---|---:|---:|---:|---:|
| `configs/d213-minimal.config` | 63696 | 1056 | 14536 | 79288 |
| `configs/d213-default.config` | 113688 | 4360 | 15720 | 133768 |
| 默认 + lvgl-aic-standalone 软件路径 | 本次迁移未重建 | - | - | - |

打包结果：

| 配置 | `rtthread.bin` | `d21x_os.itb` | 最终 `.img` |
|---|---:|---:|---:|
| minimal | 64800 | 67584 | 916992 |
| 默认 `.config`（含 GT911） | 118280 | 120832 | 970240 |

最终 `.img` 包含 Bootloader/ENV/PBP，具体大小随 SDK 资产变化。

### 跨平台打包验证

macOS Apple Silicon（Python 3.13 + uv venv）已执行：

```sh
AIC_SDK_DIR=/path/to/luban-lite-jc \
    python tools/mk_firmware.py --elf rtthread.elf --out images
```

检查结果：

- `d21x_os.itb` 大小为 120832，FDT magic 为 `d0 0d fe ed`；
- 最终 `.img` 大小为 970240，头 8 字节为 `AIC.FW\0\0`；
- `bootloader.aic` == `pbp_ext.aic` + `loader.aic`，未改写原厂 Bootloader；
- `env.bin` 为 4096 字节，冗余标志为 1，CRC32 正确；
- 纯 Python `mkenvimage.py` 输出与系统原生 U-Boot `mkenvimage` 逐字节一致。

SDK 工具层测试：

```sh
cd "$AIC_SDK_DIR/tools/scripts"
python -m unittest discover -s tests -v
```

测试只验证打包，不进行烧录或硬件启动验证。

Xuantie GCC 10.2.0（V2.6.1）也可构建同一源码：
`text=103304, data=5856, bss=10800, rtthread.bin=109160`。

这些数字只证明构建和链接成功，不证明外设功能。

## 17.2 基础 BSP 自检

### 启动日志（目录重组前）

```text
spl read: 46648 byte
CRC32 verify OK.
...
RT-Thread 5.3.0
ArtInChip D213 demo128 RT-Thread BSP
CPU: T-Head C906 (RV64GC), tick: 1000 Hz
worker1: fp result=526
worker2: fp result=528
selftest: counter=2000 (expected 2000), ticks=5
main: running
msh >
```

这证明：

- FIT 段加载/入口正确；
- UART0 输出；
- Tick 工作；
- 两线程切换；
- 互斥量/信号量；
- FPU 算术；
- FinSH 可用。

重组后需要重新录制同样的日志，并补上强化后的 `fpu context OK`。

### `aic_selftest`

```text
msh > aic_selftest
```

检查项：

- 计数线程是否达到预期；
- Tick 是否推进；
- FPU 结果；
- 互斥量/信号量是否死锁。

如果失败，先确认 `RT_TICK_PER_SECOND`、线程优先级和栈大小没有被改坏。

## 17.3 外设测试入口

以下外设命令需要先在 `scons --menuconfig` 中启用对应外设；默认 `.config` 包含 UART0、显示/GE 和 GT911 触摸测试命令。

| 命令 | 功能 | 需要硬件 |
|---|---|---|
| `periph_status` | 列出已注册设备 | 否 |
| `i2c_scan i2c3` | 扫描 I2C | I2C 从机/上拉 |
| `touch_test 10` | 读取 GT911 触摸点 10 秒 | 7" LVDS CTP |
| `gt911_dump` | 打印 GT911 ID、配置和状态寄存器 | GT911 |
| `gt911_reset` | 复位并重新探测 GT911 | GT911 |
| `can_baud can0 500000` | 设置 CAN 位速率 | 收发器 + 对端 |
| `can_mode can0 normal` | 设置 CAN 模式 | 收发器 + 对端 |
| `can_send can0 0x123 11 22` | 发送 CAN | 收发器 + 对端 |
| `can_dump can0 5` | 接收 CAN | 收发器 + 对端 |
| `pwm_test 0 1000000 500000 3` | PWM 输出 | 示波器/负载 |
| `gpio_toggle PC.7 5 200` | GPIO 翻转 | LED/示波器 |
| `rtc_test` | 读 RTC | 32K 晶振/电池 |
| `wdt_test feed` | 喂狗 | 看门狗已启动 |
| `wdt_test start 10` | 启动看门狗 | **会复位** |
| `disp_clk` | 显示/GE CMU 寄存器 | 否 |
| `disp_colorbar 1` | DE 硬件 Color Bar | LVDS 面板 |
| `disp_info` | 显示参数与 buffer 地址 | 否 |
| `disp_test` | Color Bar/fill/pan/VSync | LVDS 面板 |
| `ge_test` | GE Fill/BitBlt/Scale/Blend/Rotate | GE + 内存 |
| `lvgl_demo` | 启动 LVGL 9.6.0 演示 | LVDS + 外部 LVGL + GT911 |
| `lvgl_demo status` | 显示 flush/FPS/flush_ms/触摸状态 | LVGL 已启动 |
| `lvgl_touch 10` | 统计 LVGL 触摸坐标范围 | LVGL + GT911 |
| `lvgl_demo bench 10` | 持续动画测量 presented FPS | LVGL 已启动 |

## 17.4 GPIO 测试

### 输出

```text
msh > gpio_toggle PC.7 10 500
```

预期：PC.7 每 500 ms 翻转一次。用 LED 或示波器确认。

### 输入

```c
rt_pin_mode(rt_pin_get("PA.12"), PIN_MODE_INPUT_PULLUP);
rt_kprintf("%d\n", rt_pin_read(rt_pin_get("PA.12")));
```

### 中断

```c
rt_pin_attach_irq(pin, PIN_IRQ_MODE_FALLING, handler, RT_NULL);
rt_pin_irq_enable(pin, PIN_IRQ_ENABLE);
```

用按键或信号发生器触发，统计 handler 次数。

### 常见问题

| 现象 | 原因 |
|---|---|
| 读回不变 | 引脚被外设占用/功能不是 GPIO |
| 中断不进 | 未 `rt_pin_irq_enable`、PLIC 未 umask |
| 中断风暴 | 电平触发且未清除/外部一直有效 |
| 输出无变化 | 引脚被其他外设覆盖、负载过大 |

## 17.5 UART 测试

### 回环

TX 短接 RX，发送数据并读回：

```c
rt_device_write(uart, 0, "0123456789", 10);
rt_device_read(uart, 0, buf, sizeof(buf));
```

### 双机

两块板交叉 TX/RX/GND，分别 115200 和 921600，连续发送 1 MB。

### 常见问题

| 现象 | 原因 |
|---|---|
| 无输出 | pinmux/时钟/波特率 |
| 乱码 | 时钟频率或波特率不匹配 |
| 丢字节 | RX FIFO/ring buffer 太小、ISR 延迟 |
| 只能发不能收 | RX 未使能、PA.1 未上拉、对端 TX 未接 |

## 17.6 I2C 测试

```text
msh > i2c_scan i2c3
```

- 有设备但读写失败：地址/寄存器/时序；
- 全部无响应：上拉、供电、pinmux；
- 偶发超时：从机时钟拉伸、总线电容、干扰；
- SDA 一直被拉低：stuck 恢复是否生效。

用逻辑分析仪确认 START/STOP/ACK。

### GT911 触摸

```text
msh > i2c_scan i2c3
msh > gt911_dump
msh > touch_test 10
```

2026-11-05 实机结果：

```text
gt911: found at 0x5d, id 911
id: 39 31 31 00 60 10
cfg: 81 00 04 58 02 05 3d 20
touch id=0 DOWN x=502 y=35 w=17
touch id=1 DOWN x=616 y=156 w=43
touch id=2 DOWN x=733 y=250 w=23
touch id=3 DOWN x=974 y=155 w=40
```

多点 DOWN/MOVE 已通过；X/Y 方向和是否需要 swap/flip 还需按面板
四角坐标校准。`gt911_dump` 用于查看 ID、配置和 `0x814E` 状态：

- `0x80`：有数据待读取；
- 低 4 位：触摸点数；
- 长时间为 0 且触摸无反应：检查复位、INT、供电和 I2C3。

## 17.7 CAN 测试

### 最小接线

```text
CAN_H <-> CAN_H
CAN_L <-> CAN_L
GND   <-> GND
120 Ω 两端
```

### 步骤

1. 双方 500 kbps；
2. 对端发标准帧 0x123，D213 `can_dump can0 5`；
3. D213 `can_send can0 0x123 11 22 33`，对端确认；
4. 改扩展帧 0x18FF1234；
5. 改 0-8 字节长度；
6. 配置过滤器；
7. 断开总线，观察错误计数；
8. 恢复总线，确认 bus-off 恢复；
9. 1 Mbps 连续发送。

### 常见问题

| 现象 | 原因 |
|---|---|
| 发送失败 | 无 ACK、总线只有单节点、波特率不一致 |
| 收不到 | 过滤器、模式、终端电阻、CAN_H/L 反接 |
| bus-off | 错误累积、无对端、干扰 |
| 过滤器无效 | 标准/扩展不匹配、双过滤器限制 |
| 数据错位 | DLC/字节序/结构体未清零 |

## 17.8 PWM 测试

```text
msh > pwm_test 0 1000000 500000 3
```

示波器检查：

- 频率 1 kHz；
- 高电平 500 us；
- 3 秒后停止。

如果无输出：

- PC.0/PC.1 是否被 SDMC1 占用；
- PWM 时钟是否使能；
- 通道号是否 0-3；
- 负载是否把引脚拉死。

## 17.9 WDT 测试

**高风险。**

```text
msh > wdt_test start 10
msh > wdt_test feed
```

如果不喂狗，10 秒后复位。用于验证：

- 看门狗计数；
- 复位路径；
- 复位后串口是否重新启动。

不要在没有恢复手段时测试。

## 17.10 RTC 测试

```text
msh > rtc_test
```

检查：

- 时间是否合理；
- 断电 10 分钟后是否继续走；
- 32K 晶振是否起振；
- 写入/读回是否一致。

## 17.11 压力测试建议

| 项目 | 方法 | 目标 |
|---|---|---|
| 冷启动 | 断电/上电 100 次 | 每次都进入 MSH |
| Tick | 运行 24 h，比较 `rt_tick_get` 与 RTC | 无累积漂移 |
| 调度 | 多线程 + IPC 循环 | 无死锁/优先级反转 |
| UART | 1 MB 连续收发 | 无丢字节 |
| I2C | 10 万次读写 | 无超时/总线锁死 |
| CAN | 1 Mbps 连续帧 | 无丢帧/错误增长 |
| GPIO | 高频翻转 + 中断 | 无中断丢失 |
| 内存 | 反复 malloc/free | 无泄漏/碎片 |
| 异常 | 非法地址/栈溢出 | 有可读诊断 |

这些测试目前**未执行**。

## 17.12 显示与 GE 测试

### 硬件 Color Bar

```text
msh > disp_colorbar 1
msh > disp_colorbar 0
```

预期：彩条出现/消失。若彩条不出现，优先检查：

- LVDS 时钟（52 MHz / 364 MHz）；
- LVDS link 选择（demo128 为 link 1，PD.8-PD.17）；
- 面板使能 PC.7；
- DE timing 的极性/porch；
- 背光和面板供电。

彩条不依赖 Framebuffer，因此是分离“显示接口问题”和“Framebuffer 问题”的第一道测试。

### 实机结果（2026-11-04）

```text
disp_colorbar 1        -> hardware color bar on，面板显示彩条
disp_info              -> 1024x600, 32 bpp, stride 4096
                          buffers: 2 x 2457600 bytes
                          fb0=0x45800000 fb1=0x45a58000
disp_test              -> pan fb0 (red): OK
                          pan fb1 (green): OK
                          vsync: 10/10 received
                          display stats: vsync=4692 underflow=0 active=1
ge_test（旧固件）      -> fillrect PASS, bitblt PASS, scale 2x executed
```

结论：LVDS、DE、Framebuffer、双缓冲、VSync、硬件 Color Bar 和 GE FillRect/BitBlt 已实机验证。GE 混合/旋转用例修正后尚待复测。

如果启动时打印 `lcd: display power-up failed`，先执行 `disp_clk`：

- `PLL_FRA2` 应为 364 MHz 对应的分频/小数配置；
- `CMU disp` 的 PIX 分频应为 7；
- `CMU de` / `CMU ge` 的 bit 13（RSTN）应为 1，分频字段应使引擎时钟为 200 MHz；
- `CMU lvds` 的 bit 13（RSTN）应为 1，门控位应打开。

### Framebuffer / VSync

```text
msh > disp_info
msh > disp_test
```

`disp_test` 会：

1. 打开硬件彩条 2 秒；
2. 用红色填 fb0 并切换；
3. 双缓冲时用绿色填 fb1 并切换；
4. 等待 10 次 VSync 并打印计数。

若 `vsync=0/10`，检查 DE IRQ 59 是否注册、`TIMING_INIT` 是否使能、DE timing 是否真正启动。

### GE

```text
msh > ge_test
```

FillRect/BitBlt 会做精确像素回读；缩放/混合/旋转打印 `executed`，因为方向/边界语义需要在实机上确认。若 GE 失败：

- 检查 `ge: version` 是否为非零；
- 检查 `GE_BASE`/`GE_IRQn`；
- 检查缓冲区地址/stride 对齐；
- 检查 Cache 维护是否执行；
- 检查是否误用了 YUV 格式（当前适配层拒绝）。

### LVGL 9.6.0

```text
msh > lvgl_demo
msh > lvgl_demo status
msh > lvgl_touch 10
msh > lvgl_demo bench 10
msh > lvgl_demo stop
```

页面验证项：

- `TL/TR/BL/BR` 四角标记和中心十字；
- `CLICK` 按钮计数递增；
- `DRAG` 滑块可按住拖动并更新百分比；
- `lvgl_demo status` 中 flush/FPS/flush_ms 持续变化；
- `lvgl_touch 10` 记录四角触摸范围，用于判断 swap/flip；
- `lvgl_demo bench 10` 记录持续动画下的实际 presented FPS。

若启动失败：

- 检查 `BSP_USING_DISPLAY` 和 32 bpp Framebuffer；
- 检查 `BSP_USING_LVGL`；
- 检查 `LVGL_DIR/lvgl.h` 是否存在；
- 检查 `LVGL_DIR/include/lvgl/lv_version.h` 是否为 9.6.x；
- 检查 `LVGL_AIC_DIR` 是否指向 `lvgl-aic-standalone`；
- 检查 `rt_malloc_align` 是否成功（约 2.4 MB）；
- 检查 LVGL 线程栈 32 KB 是否足够。

以下为迁移前 BSP 内 LVGL 端口的实机结果；新的
`lvgl-aic-standalone` 镜像需要重新执行同样步骤，不能直接沿用 HW 结论。

2026-11-05 实机结果（旧 BSP 内端口，软件渲染路径）：

```text
msh > lvgl_demo
lvgl: started
lvgl: touch gt911 range 1024x600

msh > lvgl_touch 10
lvgl_touch: events=487 samples=133 extent x=9..1014 y=55..558
```

`lvgl_demo` 页面正常上屏，CLICK 和 DRAG 均可用；坐标范围与四角标签
`TL/TR/BL/BR` 的内缩位置一致，默认方向无需 swap/flip。边缘有效区如需
精调，再使用 `LVGL_AIC_TOUCH_SWAP_XY/X_FLIP/Y_FLIP`。

若显示正常但无触摸：

1. 先运行 `touch_test 10` 确认 GT911 原始触摸；
2. 检查 `LVGL_AIC_USE_TOUCH` 和 `LVGL_AIC_TOUCH_DEVICE="gt911"`；
3. 检查 `lvgl_demo status` 是否显示 `touch=gt911`；
4. 用 `LVGL_AIC_TOUCH_SWAP_XY/X_FLIP/Y_FLIP` 修正方向。

第一阶段不启用 FreeType、MPP 图片解码、GE2D Draw Unit 和复杂页面。

## 17.13 构建问题

见 [15-build-pack-deploy.md](15-build-pack-deploy.md) 的“常见构建失败”。

## 17.14 启动问题

### 无任何串口输出

1. 供电；
2. 串口线 TX/RX/GND；
3. 波特率 115200；
4. Bootloader 是否运行；
5. 启动介质是否可读。

### 有 Bootloader 无 RT-Thread

1. 镜像是否写入 `os` 分区；
2. FIT 加载地址/入口；
3. 镜像大小是否超过分区；
4. `rtthread.elf` 是否真的更新（时间戳/CRC）；
5. 串口是否在 `rt_hw_board_init` 前被改引脚。

### 启动后反复复位

1. 看门狗是否被意外启动；
2. 堆是否覆盖 Bootloader；
3. 中断是否未清导致风暴；
4. 电源是否跌落。

## 17.15 外设初始化问题

| 现象 | 检查 |
|---|---|
| `periph_status` 显示 missing | Kconfig 是否启用、SConscript 是否加入源文件、init 是否返回错误 |
| 初始化卡住 | HAL 轮询等待时钟/总线；检查时钟门控和引脚 |
| 初始化后系统变慢 | 中断风暴/忙等 |
| 只有部分实例可用 | 引脚冲突、时钟表缺失 |
| 重新编译后行为变化 | `.config`/`rtconfig.h` 不同步 |

## 17.16 已知缺陷

- FPU `fcsr` 未保存；
- GPIO 开漏未真正实现；
- GPIO 去抖没有毫秒换算；
- RTC 闹钟未实现；
- CAN 状态码映射不完整；
- I2C 仅主机、阻塞；
- UART 无 DMA/流控/RS485；
- 显示/GE 尚未实机验证；
- 显示仅支持 LVDS；MIPI/RGB、触摸尚未实现；
- GE 仅 RGB、Normal 模式，无 CMD Queue；
- LVGL 源码作为外部依赖，不随 BSP 提交，且没有触摸输入；
- 没有存储/网络/USB/音频/视频；
- 没有重组后的实机回归记录。

这些问题在对应文档中都有说明，不应在发布说明中隐藏。
