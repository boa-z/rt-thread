# 13 显示、图形加速（GE）与 LVGL 9.6.0

## 13.1 当前状态

截至 2026-11-04，以下功能已在 D213 demo128 实机验证：

- D21x Display Engine（DE）UI 层初始化；
- LVDS 输出（1024×600，VESA-24，single link 1）；
- 硬件 Color Bar；
- RT-Thread Graphic Device（`lcd`）；
- 单/双 Framebuffer 与 fb0/fb1 切换；
- DE VSync 中断与等待（`10/10`，`underflow=0`）；
- GE Normal 模式 FillRect、BitBlt；
- GT911 触摸：多点 DOWN/MOVE/UP 读取通过；
- LVGL 9.6.0 软件渲染上屏、CLICK、DRAG 和 GT911 坐标：实机通过。

仍在验证中：

- GE 缩放、Alpha 混合、旋转：测试用例已修正，待实机复测；
- GT911 触摸边缘坐标/中断模式：待校准；
- LVGL GE 帧拷贝路径：待实机对比。

仍未实现：

- MIPI DSI / RGB 输出；
- GE CMD Queue；
- YUV 格式的 GE 操作；
- 面板 PWM 调光（demo128 的 PC.7 是 GPIO 使能）；
- LVGL 图片解码、文件系统资源和 FreeType。

## 13.2 代码组织

| 文件 | 职责 |
|---|---|
| `vendor/display/aic_hal_de.c` | 原厂 DE 寄存器 HAL，逐字节未修改 |
| `vendor/include/aic_hal_de.h` | DE 寄存器/API |
| `vendor/include/aic_hal_lvds.h` | LVDS 寄存器定义 |
| `vendor/port/aic_disp_clk.c` | PLL_FRA2、SCLK、PIXCLK 显示时钟 |
| `vendor/port/aic_hal_reset.c` | DE/GE/LVDS 复位 |
| `board/cache.c` | C906 D-Cache clean/invalidate |
| `drivers/drv_disp.c` | LVDS + DE + Framebuffer + VSync |
| `drivers/drv_disp.h` | 显示 API |
| `drivers/drv_ge.c` | GE Normal 适配、缓存维护、串行化 |
| `drivers/drv_ge.h` | GE API |
| `vendor/ge/hal_ge_normal.c` | 原厂 GE Normal HAL，逐字节未修改 |
| `vendor/ge/hal_ge_hw.c` | 原厂 GE 寄存器操作，逐字节未修改 |
| `applications/display_test.c` | `disp_*` / `ge_test` 测试命令 |
| `lvgl/SConscript` | 外部 `lvgl-aic-standalone` 集成入口 |
| 外部 `lvgl-aic-standalone` | LVGL runtime、display/input port、GE backend、demo |

## 13.3 硬件与板级参数

D213 demo128 官方 7 英寸 LVDS 套件的常用参数：

| 参数 | 值 |
|---|---|
| 分辨率 | 1024×600 |
| 像素时钟 | 52 MHz |
| HFP / HBP / HSW | 160 / 160 / 20 |
| VFP / VBP / VSW | 20 / 12 / 2 |
| 同步极性 | HSYNC low，VSYNC low |
| LVDS 映射 | VESA-24 |
| LVDS link | single link 1（PD.8-PD.17） |
| 面板使能 | PC.7，高有效（可按 Kconfig 改） |
| Framebuffer | XRGB8888，32 bpp |

这些值来自 Luban-Lite 的 `panel_simple.c` / `disp_conf.h` 和 demo128 板级 pinmux。若使用不同面板，必须修改 Kconfig 中的 timing、link 和使能 GPIO，不能直接套用。

## 13.4 Kconfig

```text
BSP_USING_DISPLAY=y
BSP_DISPLAY_WIDTH=1024
BSP_DISPLAY_HEIGHT=600
BSP_DISPLAY_PIXCLOCK=52000000
BSP_DISPLAY_HFP=160
BSP_DISPLAY_HBP=160
BSP_DISPLAY_HSW=20
BSP_DISPLAY_VFP=20
BSP_DISPLAY_VBP=12
BSP_DISPLAY_VSW=2
BSP_DISPLAY_LVDS_LINK=1
BSP_DISPLAY_LVDS_MODE=0
BSP_DISPLAY_FB_COUNT=2
BSP_DISPLAY_PANEL_ENABLE_GPIO="PC.7"
BSP_DISPLAY_PANEL_ENABLE_LOW=n
AIC_FB_RESERVED_SIZE=0x2000000

BSP_USING_GE=y
```

仓库根目录 `.config` 默认启用上述显示/GE 选项；LVGL 默认关闭。需要 LVGL 时执行 `scons --menuconfig` 打开 `BSP_USING_LVGL`，再执行 `scons --pyconfig-silent`。

## 13.5 显示时钟

`aic_disp_clk_set()` 做三件事：

1. 根据 LVDS link 计算串行时钟 `sclk = pixclk * 7`（single link）；
2. 将 `PLL_FRA2` 设为 `sclk`（保证在 360-1584 MHz VCO 范围）；
3. 配置 `CLK_DISP_REG` 的 SCLK/PIX 分频。

1024×600 时：

```text
pixclk = 52 MHz
sclk   = 364 MHz
PLL2   = 364 MHz
SCLK   = PLL2 / 1
PIXCLK = SCLK / 7
```

PLL_FRA2 按原厂流程先切到 24 MHz 旁路，写入整数/小数分频，再使能并等待固定 200 us，最后切回 PLL 输出；D21x 的 `PLL_LOCK_BIT` 不作为新频率锁定判据。

`CLK_DE`/`CLK_GE` 的父时钟为 PLL_INT1（1.2 GHz），`hal_clk_set_freq()` 将其分频到 200 MHz；`CLK_LVDS` 的门控/复位由 `hal_clk_enable_deassertrst()` 完成。可用 `disp_clk` 查看 PLL_FRA2、DISP、DE、GE、LVDS 寄存器。

**限制：** 当前只支持 single link 的整数分频。若需要 dual link 或特殊像素时钟，必须扩展 `aic_disp_clk_set()` 并验证 PLL/分频。

## 13.6 Framebuffer 与内存

显示配置默认保留 32 MB：

```text
0x40000000  RT-Thread 镜像
            ...
0x45800000  __fb_start（32 MB 显示保留区）
0x47800000  __fb_end / heap end
0x47B80000  Bootloader 保留区
0x48000000  DDR end
```

对应链接符号：

```text
__fb_start = ORIGIN + LENGTH - BOOTLOADER_RESERVED - AIC_FB_RESERVED_SIZE
__fb_end   = ORIGIN + LENGTH - BOOTLOADER_RESERVED
__heap_end = __fb_start
```

因此：

- 最小配置（`AIC_FB_RESERVED_SIZE=0`）堆布局不变；
- 显示配置下堆不会覆盖 Framebuffer；
- Framebuffer 不会被 `malloc` 分配，也不会和 Bootloader 重叠；
- 地址即物理地址（当前 BSP 无 MMU）。

1024×600×4 单缓冲 = 2,457,600 字节；双缓冲 = 4,915,200 字节，远小于 32 MB 保留区。

## 13.7 RT-Thread Graphic Device

设备名：`lcd`

```c
rt_device_t dev = rt_device_find("lcd");
struct rt_device_graphic_info info;

rt_device_open(dev, RT_DEVICE_FLAG_RDWR);
rt_device_control(dev, RTGRAPHIC_CTRL_GET_INFO, &info);
```

支持的控制命令：

| 命令 | 行为 |
|---|---|
| `RTGRAPHIC_CTRL_GET_INFO` | 返回分辨率、bpp、stride、framebuffer、smem_len |
| `RTGRAPHIC_CTRL_GET_MODE` | 返回 32-bit ARGB/XRGB 容器格式 |
| `RTGRAPHIC_CTRL_RECT_UPDATE` | clean 当前 Framebuffer 的 D-Cache |
| `RTGRAPHIC_CTRL_PAN_DISPLAY` | 切换 buffer，等待 VSync 后更新 DE 地址 |
| `RTGRAPHIC_CTRL_WAIT_VSYNC` | 等待一次 DE VBLANK |
| `RTGRAPHIC_CTRL_POWERON/OFF` | 打开/关闭 LVDS、DE timing 和面板使能 |
| `RTGRAPHIC_CTRL_GET_STATUS` | 返回电源状态 |
| `RTGRAPHIC_CTRL_SET_BRIGHTNESS` | 返回 `-RT_ENOSYS`；demo128 没有 PWM 背光 |

`RTGRAPHIC_CTRL_PAN_DISPLAY` 的参数是 `rt_uint32_t *`（buffer index）；传 `NULL` 时自动切换下一个 buffer。

## 13.8 单/双缓冲与 VSync

### 单缓冲

`BSP_DISPLAY_FB_COUNT=1`：

- 应用直接绘制 `fb[0]`；
- `RTGRAPHIC_CTRL_RECT_UPDATE` clean Cache；
- 由于 DE 持续扫描，绘制期间可能出现撕裂；
- 适合静态界面或低速刷新。

### 双缓冲

`BSP_DISPLAY_FB_COUNT=2`：

```c
int back = (active + 1) % 2;
void *fb = aic_disp_get_framebuffer(back);

/* draw into fb ... */
aic_disp_pan(back, 1);   /* clean, wait VSync, switch DE address */
```

`aic_disp_pan()` 会在切换前 clean 目标 Framebuffer，并等待 DE VBLANK。若 VSync 超时，仍会切换 buffer，但返回错误，调用者应记录/处理。

### VSync

- DE IRQ：59；
- 中断中清 `TIMING_STATUS`，V_BLANK 时 `rt_completion_done()`；
- 等待 API 使用 completion，不会累积历史事件；
- `TIMING_INIT_UNDERFLOW` 计数通过 `aic_disp_get_stats()` 读取。

```c
rt_uint32_t vsync, underflow;
int active;
aic_disp_get_stats(&vsync, &underflow, &active);
```

## 13.9 硬件 Color Bar

DE 支持内部 Color Bar 模式，不依赖 Framebuffer：

```text
msh > disp_clk
msh > disp_colorbar 1
msh > disp_colorbar 0
```

`disp_clk` 打印 PLL_FRA2、DISP、DE、GE、LVDS 的 CMU 寄存器，用于确认分频/门控/复位状态。

实现：

```c
de_config_update_enable(DE_BASE, 0);
de_ui_layer_enable(DE_BASE, 0);
de_colorbar_ctrl(DE_BASE, 1);
de_config_update_enable(DE_BASE, 1);
```

Color Bar 是验证 LVDS 时钟、lane 映射、DE timing 的最直接手段。即使 Framebuffer 尚未正确配置，也应先看到硬件彩条。

## 13.10 GE Normal 模式

GE 由原厂 Normal HAL 实现，未启用 CMD Queue。

### 支持的操作

| 操作 | API | 说明 |
|---|---|---|
| FillRect | `aic_ge_fillrect()` | 纯色/水平/垂直渐变 |
| BitBlt | `aic_ge_bitblt()` | 拷贝、裁剪、翻转、90/180/270 旋转 |
| Scale | `aic_ge_bitblt()` | 源/目标尺寸不同时由 scaler 完成 |
| Blend | `aic_ge_bitblt()` | Porter/Duff alpha、global alpha、color key |
| Rotate | `aic_ge_bitblt()` / `aic_ge_rotate()` | 90° 步进或任意角度 |

### 缓冲区结构

```c
struct mpp_buf src = {
    .buf_type = MPP_PHY_ADDR,
    .phy_addr[0] = (uintptr_t)src_buf,
    .stride[0] = width * 4,
    .size.width = width,
    .size.height = height,
    .format = MPP_FMT_XRGB_8888,
};
```

GE API 会在操作前后执行 Cache 维护，并串行化对控制器的访问。调用者必须保证：

- `phy_addr[0]` 是有效物理地址；
- `stride[0] >= width * bytes_per_pixel`；
- 缓冲区不重叠（除明确支持的 blend）；
- RGB 格式；当前适配层拒绝 YUV。

### 示例：FillRect

```c
struct ge_fillrect fill = {0};

fill.type = GE_NO_GRADIENT;
fill.start_color = 0x000000FF; /* BGR memory order: blue */
fill.dst_buf = dst;
aic_ge_fillrect(&fill);
```

### 示例：缩放

```c
struct ge_bitblt blt = {0};

blt.src_buf = src;              /* 160x120 */
blt.dst_buf = dst;              /* 320x240 */
blt.dst_buf.size.width = 320;
blt.dst_buf.size.height = 240;
blt.dst_buf.stride[0] = 320 * 4;
aic_ge_bitblt(&blt);
```

### 示例：Alpha 混合

```c
blt.ctrl.alpha_en = 1;
blt.ctrl.alpha_rules = GE_PD_SRC_OVER;
blt.ctrl.src_alpha_mode = 0;   /* pixel alpha */
aic_ge_bitblt(&blt);
```

### 限制

- 仅使用 GE Normal 模式，不启用 CMD Queue；
- 当前适配层只接受 RGB 格式；
- RGB 缩放时源/目标尺寸都必须至少 4×4；适配层会在下发前拒绝非法尺寸，避免厂商 HAL 忽略错误后 GE 挂死；
- 旋转/缩放的精确方向、边界和色彩顺序需实机确认；
- 没有 YUV420/NV12 等格式的验证；
- 没有 GE 性能数据；
- 没有多客户端并发压力测试（适配层已用互斥量串行化）。

## 13.11 GE 测试命令

```text
msh > ge_test
```

执行：

1. 读取 GE 版本；
2. FillRect 并回读像素；
3. BitBlt 1:1 并回读；
4. 2× 缩放；
5. Alpha 混合；
6. 90° 旋转。

FillRect/BitBlt 做精确像素校验；16×16 的 Alpha 混合检查混合后的红/蓝通道；缩放/旋转打印 `executed`，因为其方向/边界语义还需要在实机上确认。命令不会把未验证结果写成 PASS。

## 13.12 LVGL 9.6.0

### 固定依赖

LVGL 和原生 RT-Thread 适配组件都不随本 BSP 提交：

```text
LVGL v9.6.0
commit 80ca777e37a2b176770726a02e07a6fb79ef0b39

lvgl-aic-standalone
https://github.com/boa-z/lvgl-aic-standalone
```

获取源码：

```sh
git clone --branch v9.6.0 --depth 1 \
    https://github.com/lvgl/lvgl.git /path/to/lvgl-9.6.0
git clone https://github.com/boa-z/lvgl-aic-standalone.git \
    /path/to/lvgl-aic-standalone

export LVGL_DIR=/path/to/lvgl-9.6.0
export LVGL_AIC_DIR=/path/to/lvgl-aic-standalone
```

BSP 的 `lvgl/SConscript` 只负责调用外部组件；LVGL runtime、display/input
port、GE backend 和 demo 均位于 `lvgl-aic-standalone`。组件会检查 LVGL
版本并只接受 9.6.x。

### Kconfig

```text
BSP_USING_LVGL=y
LVGL_AIC_USE_D21X_BACKEND=y
LVGL_AIC_USE_TOUCH=y
LVGL_AIC_TOUCH_DEVICE="gt911"
LVGL_AIC_TOUCH_SWAP_XY=n
LVGL_AIC_TOUCH_X_FLIP=n
LVGL_AIC_TOUCH_Y_FLIP=n
LVGL_AIC_USE_GE_COPY=n
LVGL_AIC_DEMO_SMOKE=y
```

第一阶段默认使用软件帧拷贝；`LVGL_AIC_USE_GE_COPY=y` 可启用已有 GE
Normal bitblt 帧拷贝。该选项依赖 `LVGL_AIC_USE_D21X_BACKEND` 和
`BSP_USING_GE`。

### 构建

```sh
export LVGL_DIR=/path/to/lvgl-9.6.0
export LVGL_AIC_DIR=/path/to/lvgl-aic-standalone
scons --menuconfig          # 打开 BSP_USING_LVGL
scons --pyconfig-silent
scons -j8
```

BSP 自动设置 `LVGL_AIC_BSP_DIR`。组件把外部 LVGL 编译到
`build/lvgl_aic/lvgl_ext/`，把自身源码编译到
`build/lvgl_aic/component/`，不会污染外部源码树。

### 显示路径

- LVGL 颜色格式：`LV_COLOR_FORMAT_XRGB8888`；
- LVGL OS 适配：官方 `LV_OS_RTTHREAD`；
- 渲染模式：`LV_DISPLAY_RENDER_MODE_FULL`；
- LVGL 在独立 32 bpp 缓冲中完成软件渲染；
- 组件 display port 通过 D21x backend flush：
  - 默认：CPU `memcpy` 到 back framebuffer；
  - 可选：`aic_ge_bitblt()` 拷贝到 back framebuffer；
- 然后 `aic_disp_pan(back, 1)`，clean Cache、等待 VSync、切换 DE 地址；
- `lv_timer_handler()` 在组件单 UI 线程中运行，周期 10 ms。

内存占用：

- LVGL 对象/样式使用 RT-Thread 堆；
- 全屏 32 bpp 绘制缓冲由组件 `rt_malloc_align(..., 64)` 分配，约 2.4 MB；
- LVGL UI 线程栈 32 KB；
- `lvgl_demo stop` 只停止刷新；`lvgl_demo deinit` 释放组件资源。

### GT911 输入

新增 `LVGL_AIC_USE_TOUCH=y` 时，组件读取 RT-Thread 的 `gt911` 设备：

- 触摸范围按 `RT_TOUCH_CTRL_GET_INFO` 映射到显示分辨率；
- 只跟踪一个主 `track_id`，忽略多指干扰；
- 默认方向为原始 X/Y；
- 如果面板方向不同，用 `LVGL_AIC_TOUCH_SWAP_XY`、`X_FLIP`、`Y_FLIP` 修正；
- 不修改 GT911 驱动，也不引入 lvgl-aic 的触摸 worker/OSAL 依赖。

### MSH 演示

```text
msh > lvgl_demo
msh > lvgl_demo status
msh > lvgl_touch 10
msh > lvgl_demo bench 10
msh > lvgl_demo stop
msh > lvgl_demo deinit
```

`lvgl_demo` 页面包含：

- 四个角 `TL/TR/BL/BR` 标记，用于确认坐标方向；
- 中心十字；
- `CLICK` 按钮，统计点击次数；
- `DRAG` 滑块，验证按住拖动；
- 实时显示触摸坐标、按下/释放状态、flush 次数和 FPS。

`lvgl_touch 10` 会在 10 秒内统计 LVGL 收到的触摸范围，依次触摸四角和中心
即可判断方向是否正确。

`lvgl_demo status` 额外输出 flush 的 last/avg/max 毫秒值。静态页面只由
200 ms 状态定时器触发刷新，显示约 5 fps 属于正常；`lvgl_demo bench 10`
会用持续动画测量实际 presented-frame 速率。

### 验证结果

macOS Apple Silicon + GCC 16.1.0 + LVGL v9.6.0 已完成软件渲染构建和打包：

| 项目 | 大小 |
|---|---:|
| `rtthread.bin` | 590688 |
| `d21x_os.itb` | 593920 |
| 最终 `.img` | 1443328 |

该大小是 `lvgl-aic-standalone` 集成后的构建/打包结果。旧的在 BSP 内
LVGL 端口曾完成实机显示、CLICK、DRAG 和坐标验证，但该 HW 结论不自动
转移到新组件；新组件镜像仍需按 13.13 重新上板确认。

### 第一阶段范围

本阶段不实现：

- FreeType；
- MPP 图片解码；
- GE2D Draw Unit；
- 复杂应用页面；
- LVGL 文件系统和图片资源。

## 13.13 验证状态

| 项目 | 状态 |
|---|---|
| DE HAL 编译 | BUILD |
| LVDS 寄存器序列 | HW |
| 显示时钟 PLL/分频 | HW |
| Framebuffer 保留区 | HW |
| RT-Thread `lcd` 设备 | HW |
| 双缓冲/VSync | HW |
| 硬件 Color Bar | HW |
| GE Normal FillRect | HW |
| GE Normal BitBlt | HW |
| GE 缩放/混合/旋转 | BUILD/STATIC（测试用例已修正，待复测） |
| LVGL 9.6.0 端口（standalone） | BUILD + PACKAGE（官方 v9.6.0，macOS GCC 16.1.0） |
| LVDS 实机点亮 | **HW（2026-11-04）** |
| GT911 触摸驱动 | HW（多点 DOWN/MOVE/UP 读取通过） |
| LVGL 触摸输入（standalone） | BUILD + PACKAGE，实机待验证 |

实机记录（2026-11-04）：

```text
disp_colorbar 1        -> hardware color bar on，面板显示彩条
disp_info              -> 1024x600, 32 bpp, 2 x 2457600 bytes
                          fb0=0x45800000 fb1=0x45a58000
disp_test              -> pan fb0 (red): OK
                          pan fb1 (green): OK
                          vsync: 10/10 received
                          display stats: vsync=4692 underflow=0 active=1
ge_test（旧固件）      -> fillrect PASS, bitblt PASS, scale 2x executed
```

实机验收顺序：

1. `disp_colorbar 1`，确认 LVDS 彩条；
2. `disp_info`，确认 1024×600、双缓冲、地址；
3. `disp_test`，确认 fill/pan/VSync；
4. `ge_test`，确认 GE 基本操作；
5. `lvgl_demo`，确认软件渲染上屏、CLICK 和 DRAG；
6. `lvgl_demo status`，确认 flush/FPS/flush_ms；
7. `lvgl_touch 10`，依次触摸四角和中心，确认坐标方向；
8. `lvgl_demo bench 10`，记录持续动画下的 presented-frame 速率；
9. 方向不符时用 Kconfig 的 swap/flip 修正后复测；
10. 记录 underflow、VSync、FPS/flush_ms 和画面现象，再更新验证状态。
