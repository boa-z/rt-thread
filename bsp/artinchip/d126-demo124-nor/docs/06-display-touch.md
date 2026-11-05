# D126 显示与触摸：现状和计划

## 1. 当前状态

未实现。以下参数来自原厂 D126 target 的默认配置，**尚未在
D126FxR-DEMO-V1-1 实物上确认**：

| 项目 | 参考值 | 确认状态 |
|---|---|---|
| 显示接口 | MIPI DSI | 官方页面确认 |
| Panel | EK79007AD | D126 defconfig 选择，待实物 |
| 分辨率 | 1024x600 | 参考 panel 参数，待实物 |
| 像素格式 | RGB565 | D126 defconfig，待实物 |
| Panel enable | PF.0 | D126 defconfig，待实物 |
| 触摸 IC | GT911 | 参考同系列配置，D126 target 未显式选择 |
| 触摸总线 | I2C0 | 待确认 |
| 触摸地址 | 0x5d | 参考 GT911 默认地址，待确认 |
| 触摸复位 | 待确认 | 不在 D126 target 配置中 |
| 触摸中断 | 待确认 | 不在 D126 target 配置中 |

这些值只能作为调查线索，不能直接写死为本板已验证参数。

## 2. 为什么不能复用 D213 显示代码

D213 使用：

- LVDS + DE；
- 64 字节 cache line；
- DDR framebuffer；
- 大容量全屏双缓冲。

D126 使用：

- MIPI DSI；
- 32 字节 cache line；
- 8 MiB PSRAM（默认保守配置）；
- 更小的 framebuffer 预算。

因此不能直接套用 D213 的 LVDS/DE/GE 实现。

## 3. 实施顺序

### 阶段 A：确认硬件

1. 原理图确认 MIPI DSI 通道、时钟 lane、复位、背光、电源；
2. 确认 panel 型号、初始化序列、时序、像素格式；
3. 确认 GT911 的 I2C 实例、SCL/SDA、RST/INT、上拉；
4. 用示波器/逻辑分析仪确认 I2C 和 DSI 信号。

### 阶段 B：I2C + GT911

1. 实现 D12p I2C0 时钟/复位和 pinmux；
2. 注册 RT-Thread I2C bus；
3. 读 GT911 ID（0x8140 寄存器）确认地址和通信；
4. 注册 `rt_touch_device`，实现读点；
5. 用 MSH 命令打印坐标范围。

### 阶段 C：MIPI DSI + Panel

1. 配置 DSI PHY、lane、像素时钟；
2. 发送 panel 初始化序列；
3. 配置 DE/图层和 framebuffer；
4. 色条测试，确认时序和颜色顺序；
5. 加入 VSync 和双缓冲/局部刷新；
6. 处理 32 字节 cache line 的 clean/invalidate。

### 阶段 D：GE 和 LVGL（可选）

1. 仅在基础 framebuffer 稳定后启用 GE Normal；
2. LVGL 使用外部 `lvgl-aic-standalone` 组件，不进入 BSP 核心；
3. 优先配置局部绘制缓冲，不默认全屏双缓冲；
4. 预留 framebuffer 区域，避免和 heap 冲突。

## 4. PSRAM 预算

默认 8 MB：

```text
0x40000000  RT-Thread 镜像 + heap
0x40800000  PSRAM end
```

1024x600 RGB565 单帧约 1.2 MB，双缓冲约 2.4 MB。当前 heap 约
8 MB，理论可容纳，但必须显式预留 framebuffer，不能放在 heap 中间。

建议后续在链接脚本中增加：

```text
AIC_FB_RESERVED_SIZE
__fb_start / __fb_end
__heap_end = __fb_start
```

并在显示驱动中只使用 `__fb_start..__fb_end`。具体大小在 panel
格式和缓冲策略确认后决定。

## 5. 验收标准

- [ ] I2C0 能读到 GT911 ID；
- [ ] 触摸坐标方向正确，四角和中心可区分；
- [ ] DSI 能输出稳定色条；
- [ ] panel 初始化后无花屏/闪烁；
- [ ] VSync 无 underflow；
- [ ] framebuffer 与 heap 不重叠；
- [ ] cache clean/invalidate 后 CPU 和 DMA 数据一致；
- [ ] 连续刷新 10 分钟不复位；
- [ ] 以上全部在 D126 实物上完成。

在完成前，显示/触摸状态保持 PENDING。
