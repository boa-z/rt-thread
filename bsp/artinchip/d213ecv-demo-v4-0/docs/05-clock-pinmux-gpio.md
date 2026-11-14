# 05 Clock、Reset、Pinmux 与 GPIO 基础设施

## 5.1 设计原则

D21x 的 PLL、CPU/AXI/AHB/APB 分频和 DDR 时钟由原厂 Bootloader 完成。BSP 不重建复杂 Clock Framework，只维护：

- 外设总线/模块门控；
- 外设复位释放/assert；
- HAL 需要的固定输入频率；
- 板级 pinmux。

这样做的好处：

- 不复制 Luban-Lite 的 CMU 框架；
- 不和 Bootloader 的时钟树冲突；
- 驱动拿到的频率与板级配置一致；
- 新增外设只需在表中增加一项。

## 5.2 代码组织

| 文件 | 职责 |
|---|---|
| `board/clock.c` | 启动时使能 UART/GPIO/GTC 门控 |
| `../libraries/soc/d21x/port/aic_hal_clk.c` | 为 HAL 提供 `hal_clk_*` |
| `board/pinmux.c` | 板级 UART 引脚复用 |
| `../libraries/soc/d21x/drivers/drv_gpio.c` | GPIO/Pin 设备 |
| `../libraries/soc/d21x/hal/aic_hal_gpio.c` | 原厂 GPIO 寄存器操作 |

`board/clock.c` 和 `../libraries/soc/d21x/port/aic_hal_clk.c` 都会写 CMU，但职责不同：

- `board/clock.c` 只处理启动必需的外设（UART、GPIO、GTC）；
- `../libraries/soc/d21x/port/aic_hal_clk.c` 在 HAL 初始化时按需开门控、释放复位。

两者使用相同的寄存器布局和位定义。

## 5.3 CMU 寄存器模型

```text
CMU_BASE = 0x18020000
peripheral gate register:
  bit 12 : bus clock gate
  bit 13 : reset deassert (RSTN)
  bit 8  : module clock gate (部分外设)
```

例如 UART1：

```text
offset = 0x0844
bus gate = bit 12
module gate = bit 8
reset = bit 13
```

`hal_clk_enable_deassertrst(CLK_UART1)` 的顺序：

1. 置 bus gate；
2. 置 module gate（如有）；
3. 等待 30 us；
4. 置 RSTN；
5. 等待 30 us。

## 5.4 频率表

`aic_hal_clk.c` 返回的是**板级确定值**，不是动态计算值：

| 时钟 ID | 频率 | 依据 |
|---|---|---|
| `CLK_UART0..7` | 48 MHz | 原厂 Kconfig 默认和 `sys_clk.c` |
| `CLK_I2C0..3` | 24 MHz | APB1 |
| `CLK_CAN0/1` | 24 MHz | APB1 |
| `CLK_GPAI` | 24 MHz | APB1 |
| `CLK_PWM` | 48 MHz | `PWM_CLK_RATE` |
| `CLK_WDT` | 32.768 kHz | 32K |
| `CLK_RTC` | 32.768 kHz | 32K |
| `CLK_GPIO` | 100 MHz | APB0 |
| `CLK_CODEC` | 24.576 / 22.5792 MHz | PLL_FRA1 ÷ 20，播放时按采样率设置 |

如果产品修改了 PLL/分频，必须同步修改：

- `board/Kconfig` 的 `BSP_UART_CLK_FREQ`；
- `../libraries/soc/d21x/port/aic_hal_clk.c` 的频率表；
- 相关驱动的波特率/时序计算。

## 5.5 为什么 `hal_clk_set_freq()` 返回成功

原厂 `hal_pwm_init()` 会调用 `hal_clk_set_freq(CLK_PWM, 48000000)`。本 BSP 的时钟树由 Bootloader 固定，外设频率已由板级配置确定。因此对普通外设，`hal_clk_set_freq()`/`hal_clk_set_rate()` 接受请求但不改变硬件，返回 0，让 HAL 继续初始化。唯一例外是音频：`CLK_PLL_FRA1` 会按采样率重新编程分数分频，`CLK_CODEC` 的分频字段随之设置（见 14.6）。

这不是“假装设置成功”，而是明确的架构决策：**频率由板级配置保证，HAL 只做确认**。新增外设若真的需要动态变频，应实现完整的 CMU 子集，而不是依赖这个 no-op。

PLL_FRA1 的分数分频必须按原厂顺序完成“旁路 → 编程 → 等锁定 → 切回
PLL 输出”四步。`../libraries/soc/d21x/port/aic_hal_clk.c` 曾经漏掉最后的输出切换，
PLL 数值正确但 Codec 模块停在 24 MHz ÷ 20 = 1.2 MHz，音频实际速率
约为标称值的 1/20；详见 14.6。

## 5.6 Pinmux API

板级代码目前直接写寄存器，布局定义来自共享头文件：

```c
#include "aic_gpio_reg.h"

static void aic_pin_cfg(uint32_t group, uint32_t pin, uint32_t value)
{
    volatile uint32_t *reg;

    reg = (volatile uint32_t *)(AIC_GPIO_BASE + group * AIC_GPIO_GROUP_STRIDE +
                                pin * AIC_GPIO_PIN_STRIDE + AIC_GPIO_PIN_CFG);
    *reg = value;
}
```

字段宏同样在 `../libraries/include/aic_gpio_reg.h`：

```c
#define AIC_GPIO_PIN_FUNC(v)  ((v) & 0xf)
#define AIC_GPIO_PIN_DRIVE(v) (((v) & 0x7) << 4)
#define AIC_GPIO_PIN_PULL(v)  (((v) & 0x3) << 8)
```

该头文件只描述 D21x v1.0 与 D12p v1.2 共有的 group/pin 步长、GEN 寄存器和
`PIN_CFG` 位域；group 表、IRQ 编号和 D12p 的 debounce 字段仍由各 SoC/板级
代码维护。

新增外设时，建议在 `board/pinmux.c` 中用 `#if defined(BSP_USING_xxx)` 条件编译，避免未启用外设的引脚被覆盖。

## 5.7 引脚冲突

Pinmux 是**最后写入者生效**。当前 BSP 不提供自动冲突检测。已知冲突：

| 组合 | 冲突引脚 | 处理 |
|---|---|---|
| UART2 + I2C2 | PA.8/PA.9 | 二选一 |
| UART4 + SDMC0 | PB.6/PB.7 | 二选一 |
| UART4 + QSPI0 | PB.6/PB.7 | 二选一 |
| PWM0-3 + SDMC1 | PC.0..PC.7 | 二选一 |
| I2C0 + RGB24 | PD.6/PD.7 | 二选一 |

PA.6 只用于 KEYADC（GPAI6，功能 2）；板上 R157 10 k 上拉到 VCC3V3，
无需内部上拉，当前没有与其他已实现外设冲突。

后续如果接入 SDMC/QSPI/显示，必须在 Kconfig 中增加 `depends on !...` 或运行时引脚仲裁。

## 5.8 GPIO 基础设施

GPIO 由两层组成：

- `../libraries/soc/d21x/hal/aic_hal_gpio.c`：直接操作 GEN/CFG 寄存器；
- `../libraries/soc/d21x/drivers/drv_gpio.c`：注册 RT-Thread `gpio` pin 设备。

引脚编号：

```text
pin = group * 32 + offset
PA.0 = 0
PB.0 = 32
PC.7 = 71
PE.16 = 144
```

`rt_pin_get("PA.0")` 返回 0；`rt_pin_get("PC.7")` 返回 71。

## 5.9 新增外设时的资源清单

新增一个片上外设，至少确认：

1. 基地址；
2. 中断号；
3. CMU 寄存器偏移、bus gate 位、module gate 位、reset 位；
4. 输入时钟频率；
5. 引脚和功能号；
6. 是否与其他外设共用引脚；
7. 是否需要 DMA 请求号；
8. 是否需要 Cache 维护。

把这些信息写入 [03-hardware.md](03-hardware.md) 的支持矩阵，再开始写驱动。不要先写代码再猜硬件。
