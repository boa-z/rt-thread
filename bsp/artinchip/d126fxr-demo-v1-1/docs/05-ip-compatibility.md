# D21x / D12p IP 兼容性矩阵

本矩阵用于决定哪些代码可以共享、哪些必须保持独立。只有同时满足
“IP 版本/寄存器布局一致”和“实机验证”后，才把驱动提取到公共层。

## 1. 总表

| 模块 | D21x IP | D12p IP | 是否共享 | 共享层次 | 依据/状态 |
|---|---|---|---|---|---|
| UART | v1.0 | v1.4 | 部分 | 16550 寄存器定义、轮询收发 | 寄存器兼容；D12p 有 RXCTL/FIFO 差异 |
| GPIO | v1.0 | v1.2 | 部分 | 寄存器访问宏 | 布局一致；group 映射不同 |
| I2C | v1.0 | v1.0 | 候选 | HAL + RT-Thread adapter | HAL/总线适配器已共享；D12p 时钟/pinmux 未接入 |
| DMA | v1.0 | v3.0 | 否 | 独立 | 寄存器/通道/对齐不同 |
| GE | v1.0 | v1.1 | 否 | 独立 | 命令队列和寄存器不同 |
| DE/Display | v1.0 | v1.1 | 否 | 独立 | D21x LVDS/RGB，D12p MIPI DSI/RGB |
| QSPI | v1.0 | v3.4 | 否 | 独立 | 控制器差异大 |
| WDT | v1.0 | v1.0 | 候选 | 寄存器层 | 需核对复位/时钟 |
| RTC | v1.0 | v1.21 | 否 | 独立 | 版本跨度大 |
| CAN | Classic CAN | CAN FD v1.0 | 否 | 独立 | 不能复用 D213 Classic CAN |
| CMU | v1.0 | v3.2 | 否 | 独立 | PLL/门控/分频布局不同 |
| Cache | 64 B line | 32 B line | 否 | 独立 | DMA/显示同步不同 |
| 中断 | PLIC | CLIC | 否 | 独立 | 入口和优先级模型不同 |
| 定时器 | CLINT | CORET | 否 | 独立 | 访问方式不同 |

## 2. UART

D21x：

- `AIC_UART_DRV_V10`；
- D213 BSP 使用 `libraries/soc/d21x/drivers/drv_uart.c`（D21x 共享层）；
- 16550 兼容寄存器：RBR/THR/DLL、IER/DLH、FCR/IIR、LCR、LSR、
  USR、RFL、RXCTL。

共享实现（2026-11-09）：

- `bsp/artinchip/libraries/include/aic_uart_reg.h` 定义上述偏移、
  LSR/LCR/FCR/MCR/USR 位域和 RXCTL/HSK/HALT 扩展寄存器；
- D213 `libraries/soc/d21x/drivers/drv_uart.c` 与 D126 `board/uart.c` 都使用它；
- 偏移与本地 Luban-Lite `aic_hal_uart.c` 的 `aic_usart_reg_t` 逐项一致。

D12p：

- `AIC_UART_DRV_V14`；
- 同一套 16550 兼容寄存器；
- 本 BSP 的 UART0 控制台只使用 RBR/THR/DLL/DLH/LCR/FCR/LSR；
- 差异：FIFO 深度、RXCTL、DMA 握手、时钟源。

结论：

- 可以共享寄存器宏和轮询收发核心；
- 不应直接共享完整 `rt_serial_device` 驱动，除非核对中断/FIFO/DMA；
- D12p 当前使用自定义 char 设备，后续先做中断 RX，再考虑提取。

## 3. GPIO

D21x：

- `AIC_GPIO_DRV_V10`；
- D213 BSP 使用 `../libraries/soc/d21x/hal/aic_hal_gpio.c`；
- 通用寄存器：`GEN_IN_STAT`、`GEN_OUT_CFG`、`GEN_IRQ_EN`、
  `GEN_IRQ_STAT`、`GEN_OUT_CLR/SET/TOG`、`PIN_CFG`；
- group PA..PG。

D12p：

- `AIC_GPIO_DRV_V12`；
- 同样的通用寄存器布局；
- group PA..PF、PM、PN，PM 是 PS 的硬件别名；
- GPIO IRQ 68..75，当前只使用 PA..PF 的 68..73；
- `PIN_CFG` 增加 debounce 字段。

共享实现（2026-11-09）：

- `bsp/artinchip/libraries/include/aic_gpio_reg.h` 定义 group/pin
  步长、GEN 寄存器、`PIN_CFG` 及 FUNC/DRIVE/PULL/IRQ/DIR 位域；
- D213 `board/pinmux.c` 与 D126 `drivers/drv_gpio.c` 都使用它；
- group 表、IRQ 编号和 debounce 仍留在各 SoC/板级代码。

结论：

- 可以共享寄存器偏移和位域定义；
- group 表、IRQ 映射和 debounce 必须按 SoC 独立；
- D213 适配器已移到 `libraries/soc/d21x/drivers/drv_gpio.c`；D12p 版本仍在 `drivers/drv_gpio.c`，group/IRQ/debounce 差异使其暂不共享。

## 4. I2C

证据：

```text
共享副本  : bsp/artinchip/libraries/hal/i2c/hal_i2c.c
D12p SDK  : bsp/artinchip/hal/i2c/hal_i2c.c
sha256    : f36d71d799f616946578058e752b54ee146d1a46785bda45c5d05b0d7c45e3f7
```

两个文件字节级一致（2026-11-09 重新核对），说明 I2C v1.0 控制器
在 D21x/D12p 上高度一致。头文件 `hal_i2c.h` 也逐字节一致
（`7a92d351174e7dd615ffdb385507b2742e7c7cccb98980a759408c3309027240`）。
但当前 D126 还没有 I2C 实例、时钟/复位适配和实机验证，因此：

- 状态：HAL 与总线适配器已提取，D12p 尚未接入；
- 共享层次：HAL（`libraries/hal/i2c`）+ 总线适配器（`libraries/drivers/i2c/drv_i2c.c`，`AIC_USING_DRV_I2C`）；D12p 需提供 `I2Cx_BASE`/`I2Cx_IRQn`/`CLK_I2Cx`；
- 需要先完成：D12p CMU 的 I2C0 时钟/复位、pinmux、GT911 读 ID。

## 5. DMA / GE / Display

D12p：

- DMA v3.0，8 通道，4 字节对齐；
- GE v1.1；
- DE v1.1；
- MIPI DSI v1.2；
- 32 字节 cache line。

D21x：

- DMA v1.0；
- GE v1.0；
- DE v1.0；
- LVDS/RGB；
- 64 字节 cache line。

结论：

- 不复用 D213 的 DMA/GE/DE/LVDS 驱动；
- D12p 显示需要独立的 MIPI DSI、panel、framebuffer、cache 同步；
- 只有在 D12p 显示验证后，才评估 GE Normal 的公共命令层。

## 6. CAN

D12p 是 CAN FD v1.0，D21x/D213 是 Classic CAN。两者寄存器、位时序、
FD 数据长度和错误处理不同。

结论：

- 禁止把 D213 `drv_can.c` 直接用于 D126；
- D126 需要独立 CAN FD 驱动；
- 当前未实现。

## 7. 中断和定时器

| 项目 | D21x | D12p |
|---|---|---|
| 外部中断 | PLIC | CLIC |
| 定时器 | CLINT | CORET |
| 上下文切换 | common `context_gcc.S` | common `context_gcc.S` |
| 中断优先级 | PLIC priority/threshold | CLIC CTL/MINTTHRESH |

两者现在共用 `libcpu/risc-v/common` 的 Trap、上下文切换、IRQ 栈和
Timer 核心。D213 和 D126 的差异只在 platform hook：

```text
D213: platform_get_plic_base/size/quirks + CLINT accessors
D126: platform_get_clic_base/size/quirks + CORET accessors
```

因此不再需要 D12p 自维护 `cpu/`，也不能把 D213 的 PLIC/C906 Cache
实现直接用于 D126。

## 8. 提取原则

1. 先保留 BSP 内独立实现；
2. 用第二块 D12p 板或 D133 复测；
3. 只提取“寄存器布局 + 无 SoC 条件”的代码；
4. 把 group 表、时钟、复位、IRQ、DMA 属性留在 SoC 层；
5. 任何共享重构都必须保证 D213 BUILD 和实机功能不退化。
