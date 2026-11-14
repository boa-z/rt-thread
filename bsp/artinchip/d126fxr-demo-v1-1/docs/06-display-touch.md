# D126 显示与触摸：现状和计划

## 1. 当前状态

未实现。以下参数有一部分已由 `D126FxR-DEMO-V1-1.pdf`（原理图
V1.0，2026-08-31）确认，其余仍需实物确认：

| 项目 | 参考值 | 确认状态 |
|---|---|---|
| 显示接口 | MIPI DSI，4 lane + clock | 原理图确认 |
| 显示引脚 | D0-PD18/19，D1-PD20/21，D2-PD24/25，D3-PD26/27，CK-PD22/23 | 原理图确认 |
| Panel | EK79007AD，1024x600，RGB565 | 原厂 defconfig，待实物 |
| Panel 连接器 | 32 pin FPC，含 LED+/−、VGH、VGL、AVDD、VCOM、STBYB、RESET | 原理图确认 |
| 背光 | LCD_PWM = PF.0，经 Q5 驱动 RY3750 升压 | 原理图确认 |
| Panel enable | 原 defconfig 注释的 PF.0 实为背光 PWM；STBYB/MIPI_RST 由板上电阻上拉/RC 产生 | 原理图确认，无 CPU GPIO |
| 触摸 IC | GT911（面板模组集成，原理图不体现具体 IC） | 待实物 |
| 触摸总线 | I2C0：SCL=PD.0，SDA=PD.1，33R 串阻 + 4.7k 上拉 | 原理图确认 |
| 触摸地址 | 0x5d | 参考 GT911 默认地址，待确认 |
| 触摸复位 | CTP_RST = PA.10（与 RTP_XN 复用），经 33R 到 TP_RST | 原理图确认，有效电平待确认 |
| 触摸中断 | CTP_INT = PA.11（与 RTP_YN 复用），经 33R 到 TP_INT | 原理图确认，触发沿待确认 |

PA.10/PA.11 与 RTP_XN/RTP_YN 是同一组引脚：装 CTP 时按 GPIO 使用，
装电阻屏时按 RTP 模拟输入使用，二者不能同时启用。具体地址和触摸
方向仍要读 GT911 寄存器确认。

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
   （已完成：4 lane 引脚、LCD_PWM=PF.0、STBYB/MIPI_RST 为板上
   上拉/RC，无 CPU 复位脚）
2. 确认 panel 型号、初始化序列、时序、像素格式；
3. 确认 GT911 的 I2C 实例、SCL/SDA、RST/INT、上拉；
   （接口已完成：I2C0 PD.0/PD.1，CTP_RST=PA.10，CTP_INT=PA.11；
   IC 型号、地址和方向待实物）
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
