# 16 驱动移植与扩展指南

本文面向未来维护者：如何把一个 D21x 外设从“原厂有代码”变成“本 BSP 可用、可验证、可维护”。

## 16.1 先确认硬件，再写代码

禁止根据 Luban-Lite 目录推断能力。必须确认：

1. 目标 D21x 型号是否有该控制器；
2. 目标板卡是否引出引脚/连接器/外围电路；
3. 原厂代码是否有对应 D21x 的实际调用路径；
4. 是否有条件做总线/信号测试。

把结论写入 `docs/03-hardware.md` 的矩阵。没有硬件证据时，状态只能是 SRC/BUILD/STATIC。

## 16.2 选择复用方式

按优先级：

| 方式 | 适用 |
|---|---|
| 直接复用原厂 RT-Thread 驱动 | 依赖少、与 RT-Thread 5.3 兼容 |
| 保留 HAL，重写适配层 | 原厂驱动依赖 OSAL/PM/DMA 过重 |
| 保留 HAL，直接调用 | 外设简单、无标准设备模型 |
| 复用 RT-Thread 主线驱动 | 原厂实现不如主线成熟 |
| BSP 自有实现 | 控制器简单且已验证 |

本 BSP 的现状：

- GPIO/I2C/CAN/PWM/WDT/RTC：保留 HAL，写适配层；
- UART：BSP 自有 poll+IRQ 实现；
- LVDS/DE、GE Normal：保留原厂 HAL，BSP 实现时钟、framebuffer、VSync、cache 和适配层；
- 未来 QSPI/NAND/USB：优先保留原厂 HAL，但需要完整依赖链。

## 16.3 提取原厂 HAL

步骤：

1. 找到 HAL `.c`/`.h`；
2. 记录源路径和 SHA-256；
3. 检查 `#include` 依赖；
4. 判断需要哪些兼容原语（`readl`、`BIT`、`check`、`clk`、`log`、`time`）；
5. 复制到 `vendor/hal/` 和 `vendor/include/`；
6. **不要修改** HAL 文件；只补兼容头；
7. 在 `vendor/SConscript` 中按 `BSP_USING_*` 条件编译；
8. 在 `docs/20-vendor-source-license.md` 记录。

如果必须修改 HAL，记录：

- 文件；
- 修改行；
- 原因；
- 原厂版本；
- 升级时如何合并。

## 16.4 兼容层最小集

`vendor/include/aic_compat.h` 当前提供：

- 类型：`u8..u64`、`s8..s64`、`ulong`；
- MMIO：`readb/readw/readl`、`writeb/writew/writel`、`writel_bits`；
- 位操作：`BIT`、`GENMASK`；
- 错误码：`EOK`、`EINVAL`、`ERANGE`、`EOPNOTSUPP`；
- 日志：`pr_*`、`hal_log_*`；
- 时间：`aic_get_time_ms`、`udelay`、`mdelay`；
- `do_div`；
- `irqreturn_t`；
- 参数检查宏；
- D21x 基地址/中断号；
- `AIC_PWM_CH_NUM`、`AIC_RTC_CLK_RATE` 等编译配置。

新增 HAL 时，先检查是否还需要：

- `aicos_malloc/free`；
- 信号量/互斥量；
- DMA 描述符；
- `of_*`/DTS；
- PM；
- `getopt`/FinSH；
- `csi_core`。

**不要**为了一个驱动引入完整 `aic_osal`；用 RT-Thread 原生 API 写适配层。

## 16.5 时钟/复位

在 `vendor/port/aic_hal_clk.c` 中增加：

```c
case CLK_XXX:
    desc->offset = CLK_XXX_REG;
    desc->bus_gate_bit = 12;
    desc->mod_gate_bit = 8;   /* 或 -1 */
    desc->freq = 48000000;
    return 0;
```

同时在 `board/clock.c` 中按需使能启动必需的外设。

确认：

- 寄存器偏移来自 D21x `aic_clk_id.h`；
- bus/module gate 位来自原厂 `FPCLK` 表；
- reset 位是 bit 13；
- 频率与 PLL/分频一致。

## 16.6 中断

1. 在 `aic_compat.h`/`aic_soc.h` 增加 IRQ 号；
2. 在驱动 init 中 `rt_hw_interrupt_install()`；
3. 在 open/enable 时 `rt_hw_interrupt_umask()`；
4. ISR 中清外设 status；
5. 不要在 ISR 中睡眠；
6. 在 `docs/03-hardware.md` 记录 IRQ。

## 16.7 DMA 与 Cache

如果外设需要 DMA：

1. 复制原厂 DMA HAL 或写最小描述符管理；
2. 申请通道；
3. 确认缓冲区物理地址（无 MMU，直接指针）；
4. 按 `CACHE_LINE_SIZE` 对齐；
5. 发送前 clean，接收后 invalidate；
6. 处理完成/错误中断；
7. 提供超时和 abort；
8. 在文档中说明一致性和对齐约束。

不要把“关闭 Cache”作为默认方案。

## 16.8 RT-Thread 设备接入

根据外设选择标准模型：

| 外设 | 模型 |
|---|---|
| 串口 | serial v1/v2 |
| I2C | i2c bus |
| SPI | spi |
| CAN | can |
| PWM | pwm |
| WDT | watchdog |
| RTC | rtc |
| GPIO | pin |
| ADC | adc |
| 块设备 | block/mtd |
| 显示 | framebuffer |
| 触摸 | touch/input |
| 网络 | netdev |

没有标准模型的（GE、VE、DVP、PSADC 特殊模式），用清晰的自定义 API，不要硬套。

## 16.9 Kconfig 模板

```kconfig
config BSP_USING_XXX
    bool "Enable XXX"
    default n
    select RT_USING_XXX
    help
        Describe the controller, pins, and limitations.
```

如果实例可选：

```kconfig
config BSP_USING_XXX0
    bool "Enable XXX0"
    select RT_USING_XXX
```

## 16.10 SConscript 模板

```python
if GetDepend('BSP_USING_XXX'):
    src += ['drv_xxx.c']
```

HAL：

```python
if GetDepend('BSP_USING_XXX'):
    src += ['hal/xxx.c']
```

注意 `GetDepend(['A', 'B'])` 是“全部满足”，不是“任一满足”。多个实例要用 `or`。

## 16.11 最小验证程序

每个新驱动至少提供：

1. 设备注册检查；
2. 基本读写/收发；
3. 错误路径（超时、无 ACK、总线关闭）；
4. 中断统计；
5. 压力测试入口。

MSH 命令示例：

```c
static void cmd_xxx_test(int argc, char **argv)
{
    /* 参数解析、设备查找、基本操作、结果打印 */
}
MSH_CMD_EXPORT_ALIAS(cmd_xxx_test, xxx_test, test XXX);
```

不要生成大量只为覆盖率的空测试。

## 16.12 文档更新清单

新增驱动后必须更新：

- [ ] `docs/03-hardware.md` 支持矩阵；
- [ ] 对应的外设手册；
- [ ] `docs/17-testing-troubleshooting.md` 测试入口；
- [ ] `docs/18-decisions.md` 如果做了架构选择；
- [ ] `docs/20-vendor-source-license.md` 如果复制了原厂代码；
- [ ] `README.md` 的功能列表；
- [ ] `.config` 的默认值（只维护这一份受 Git 管理的配置）。

## 16.13 新板卡移植

1. 复制 `d213-demo128-nand` 为 `dxxx-<board>`；
2. 修改 `Kconfig` 的 `BOARD_*`；
3. 修改 `board/board.h` 的内存/外设基地址（如有不同）；
4. 修改 `board/pinmux.c`；
5. 修改 `board/clock.c`；
6. 修改 `board/link.lds.S` 的 DDR 大小/保留区；
7. 修改 `pack/` 的镜像配置；
8. 修改 `rtconfig.py` 的架构/工具链；
9. 更新文档和矩阵；
10. 重新做冷启动、存储、外设验证。

不要在没有新板卡硬件的情况下创建空工程并声称支持。
