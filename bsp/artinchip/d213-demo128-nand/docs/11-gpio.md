# 11 GPIO / Pin 设备开发手册

## 11.1 设备模型

GPIO 使用 RT-Thread 标准 Pin 设备：

```text
设备名: "gpio"
PIN API: rt_pin_mode / rt_pin_write / rt_pin_read /
         rt_pin_attach_irq / rt_pin_irq_enable / rt_pin_detach_irq /
         rt_pin_debounce / rt_pin_get
```

驱动分层：

- `vendor/hal/aic_hal_gpio.c`：寄存器操作；
- `drivers/drv_gpio.c`：Pin 设备、名称解析、组中断分发。

## 11.2 引脚编号

```text
pin = group * 32 + offset
```

| 名称 | 编号 | 名称 | 编号 |
|---|---|---|---|
| PA.0 | 0 | PD.0 | 96 |
| PA.1 | 1 | PD.6 | 102 |
| PB.0 | 32 | PE.0 | 128 |
| PC.0 | 64 | PE.16 | 144 |
| PC.7 | 71 | PF.0 | 160 |

`rt_pin_get("PA.0")` 返回 0；`rt_pin_get("PC.7")` 返回 71。当前支持 D21x 的 PA..PG（7 组），每组 32 脚。D21x 还有 PU 等特殊组，未在 Pin 设备中暴露。

## 11.3 模式

| RT-Thread 模式 | 硬件配置 |
|---|---|
| `PIN_MODE_OUTPUT` | 功能 1、输出、无上下拉 |
| `PIN_MODE_OUTPUT_OD` | 当前按普通输出处理，**不是真正开漏** |
| `PIN_MODE_INPUT` | 功能 1、输入、无上下拉 |
| `PIN_MODE_INPUT_PULLUP` | 输入、上拉 |
| `PIN_MODE_INPUT_PULLDOWN` | 输入、下拉 |

开漏限制：D21x 的 pin configuration 没有独立的 open-drain 使能位；`PIN_MODE_OUTPUT_OD` 目前等价于推挽输出。需要开漏时应在板级增加外部 MOS 或确认其他寄存器/引脚属性。

## 11.4 基本使用

```c
rt_base_t pin = rt_pin_get("PC.7");

rt_pin_mode(pin, PIN_MODE_OUTPUT);
rt_pin_write(pin, PIN_HIGH);
rt_thread_mdelay(100);
rt_pin_write(pin, PIN_LOW);

rt_pin_mode(pin, PIN_MODE_INPUT_PULLUP);
int level = rt_pin_read(pin);
```

MSH：

```text
msh > pin num PA.0
msh > pin mode PC.7 output
msh > pin write PC.7 high
msh > pin read PC.7
msh > gpio_toggle PC.7 5 200
```

## 11.5 中断

```c
static void key_isr(void *args)
{
    rt_kprintf("key pressed\n");
}

rt_base_t key = rt_pin_get("PA.12");

rt_pin_mode(key, PIN_MODE_INPUT_PULLUP);
rt_pin_attach_irq(key, PIN_IRQ_MODE_FALLING, key_isr, RT_NULL);
rt_pin_irq_enable(key, PIN_IRQ_ENABLE);
```

支持：

- 上升沿；
- 下降沿；
- 双边沿；
- 高电平；
- 低电平。

实现方式：

1. 每个引脚记录 handler/args；
2. 配置该引脚的 IRQ mode；
3. 首次使用时安装组中断（IRQ 68 + group）；
4. 组 ISR 读取 status & enable，逐位清 W1C 并调用 handler；
5. `rt_pin_irq_enable()` 控制单引脚 enable。

注意：

- 电平触发如果外部一直保持有效，会反复进入 ISR；
- 组 ISR 对所有已使能引脚轮询，单个引脚高频中断会影响同组其他引脚；
- handler 在中断上下文执行，不能睡眠；
- `rt_pin_detach_irq()` 会关闭该引脚中断并清除 handler。

## 11.6 去抖

```c
rt_pin_debounce(pin, 1000);
```

参数写入硬件 `DEBOUNCE[31:20]` 字段，单位由控制器时钟决定，不是直接的微秒数。当前 BSP 没有把毫秒转换为硬件计数；调用者需要按实际时钟和寄存器语义给出值。原厂 HAL 也没有提供换算函数。

如果无法确定硬件计数，建议：

- 软件定时器/线程去抖；
- 或在板级确认 `DEBOUNCE` 的时钟源后封装一个 `aic_pin_debounce_ms()`。

## 11.7 与 Pinmux 的关系

`rt_pin_mode()` 会强制把引脚功能设为 1（GPIO）。如果该引脚已被 UART/I2C/CAN 等外设占用，调用 GPIO API 会破坏外设功能。

因此：

- 不要把 `rt_pin_mode()` 用在已分配给外设的引脚；
- 需要复用引脚时，先在 `board/pinmux.c` 中明确切换功能；
- 不要依赖“上电默认功能”做产品设计。

## 11.8 已知限制

- 仅 PA..PG 7 组；
- `PIN_MODE_OUTPUT_OD` 不是真正开漏；
- 去抖参数没有毫秒换算；
- 没有引脚冲突检测；
- 没有 GPIO 唤醒/低功耗状态保存；
- 没有做全引脚扫描测试；
- 中断 handler 在 ISR 上下文，不能直接做耗时业务。

## 11.9 测试建议

1. 输出：每个可用引脚翻转，用万用表/示波器确认；
2. 输入：外部给高/低，读回一致；
3. 上拉/下拉：悬空时读回稳定；
4. 中断：按键/信号发生器，统计中断次数；
5. 去抖：机械按键，观察抖动；
6. 冲突：启用 UART/I2C 后确认 GPIO 操作不会误改功能。

当前状态：BUILD/STATIC，未上板。
