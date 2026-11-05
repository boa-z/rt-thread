# D12p/D126 架构与移植说明

## 1. CPU

| 项目 | D12p/D126 | D21x/D213 |
|---|---|---|
| CPU | T-Head E907F | T-Head C906 |
| ISA | RV32IMafc | RV64GC |
| ABI | `ilp32f` | `lp64d` |
| FPU | 单精度 | 双精度 |
| Cache line | 32 B | 64 B |
| 中断控制器 | CLIC | PLIC |
| 定时器 | CORET | CLINT |
| 启动地址 | PSRAM `0x40000000` | DDR `0x40000000` |

因此 D213 的启动、trap、PLIC、CLINT、DDR 和 LVDS 代码不能直接复用。

## 2. 内存映射

```text
内部 SRAM     0x30040000  64 KiB（本 BSP 暂未使用）
PSRAM         0x40000000  8 MiB（默认保守配置）
SPI NOR XIP   0x60000000  16 MiB（本 BSP 未启用 XIP）
CLIC          0x20800000
CORET         0x20004000
CMU           0x18020000
UART0         0x18710000
GPIO          0x10700000
```

链接脚本把整个 RT-Thread 镜像放在 PSRAM：

```text
0x40000000  .text.start / .text / .rodata / .data / .bss
            boot stack 8 KiB
            heap -> 0x40800000
```

启动参数由原厂 Bootloader 传递，和 D21x 的 DDR 参数无关。当前 BSP
不读取 `a0/a1` 启动参数；最小启动不依赖它们。

## 3. 启动链

```text
BROM
  -> PBP（PSRAM 初始化、BROM 升级模式）
  -> SPL / bootloader
  -> d12p_os.itb
  -> RT-Thread _start
```

`d12p_os.itb` 只有一个 FIT segment，load/entry 都是 `0x40000000`。
Bootloader 把 `seg0.bin` 复制到 PSRAM 后跳转。

## 4. CLIC 和向量表

D12p 使用 CLIC 向量中断模式：

- `mtvec = Default_Handler | 3`；
- `mtvt = __Vectors`，64 字节对齐；
- `__Vectors[0..2]`、`[4..127]` 指向 `Default_IRQHandler`；
- `__Vectors[3]` 指向 `PendSV_Handler`；
- `Default_Handler` 处理 exception/NMI；
- `Default_IRQHandler` 保存 caller-saved 整数/FPU 寄存器，调用
  `rt_interrupt_enter()`、C 分发器、`rt_interrupt_leave()`。

CLIC 寄存器布局：

```text
CLICCFG      +0x0000
CLICINFO     +0x0004
MINTTHRESH   +0x0008
CLICINT[n]   +0x1000 + n*4
  IP   +0
  IE   +1
  ATTR +2
  CTL  +3
```

当前所有中断默认优先级 `0x80`，阈值 0，不做抢占。后续需要按驱动
调整优先级。

## 5. 上下文切换

E907 没有独立的 CLINT MSIP；上下文切换使用 T-Head TSPEND 寄存器：

```text
CPU_BASE + 0x80100c
```

流程：

1. `rt_hw_context_switch()` 设置 `rt_interrupt_from/to_thread`；
2. 置 `rt_thread_switch_interrupt_flag=1`；
3. 写 TSPEND bit0，挂起 CLIC 软件中断 3；
4. 中断返回后进入 `PendSV_Handler`；
5. 保存 32 个整数寄存器 + 32 个 FPU 寄存器到当前线程栈；
6. 从 `rt_interrupt_from_thread` 保存 sp，从 `rt_interrupt_to_thread`
   恢复 sp；
7. `mret` 返回新线程。

`rt_hw_stack_init()` 初始化新线程帧：

```text
frame[0]  = entry
frame[1]  = texit
frame[2]  = 0x7880   MPP=M, MPIE=1, FS=dirty
frame[10] = parameter (a0)
```

FPU 使用 `-march=rv32imafc -mabi=ilp32f`，`mstatus.FS` 在启动时置为
dirty，线程切换保存全部 32 个浮点寄存器。

## 6. Tick 和延时

CORET：

```text
MTIMECMP  +0x0000 (64-bit, 两个 32-bit 访问)
MTIME     +0x7ffc
IRQ       7
```

- `tick_interval = cpu_clock / RT_TICK_PER_SECOND`；
- 1000 Hz；
- 使用“上一次比较值 + interval”推进，落后时从当前时间重新开始；
- `rt_hw_us_delay()` 读取 64 位 MTIME 做忙等。

CPU 频率从 PLL_INT0 寄存器推导。如果 PBP 使用旁路或不同 PLL 配置，
需要修正 `clock.c`。

## 7. 时钟和 UART

`clock.c` 不重新配置 PLL，只：

1. 从 PLL_INT0/INT1 的 GEN 寄存器推导频率；
2. 为 UART0 选择分频，使模块时钟接近 24 MHz；
3. 打开 UART0 的 pre/module/APB gate，解除复位；
4. 由 UART 模块时钟计算 115200 分频。

UART0 引脚按原厂 PBP 配置：PA0=TX、PA1=RX，pinmux 值 `0x325`。
控制台当前是自定义 char 设备 + 轮询，不使用 `rt_serial_device`。
后续应改为 UART 中断 + ring buffer。

## 8. 与 D213 BSP 的边界

本 BSP 明确没有复用：

- D213 的 `start_gcc.S` / `trap.c` / PLIC / CLINT；
- D213 的 DDR 初始化、DDR bootloader 布局；
- D213 的 LVDS/DE/GE 显示实现；
- D213 的 Classic CAN 驱动。

可以复用的候选（必须经过 IP 对比和实机验证）：

- I2C HAL：D21x BSP 与 D12p SDK 的 `hal_i2c.c` 当前字节级一致；
- GPIO 寄存器访问层：布局一致，但 group 映射不同；
- UART 16550 寄存器定义和轮询核心；
- 通用 RT-Thread 设备框架、FinSH、内存管理。

## 9. 当前限制

- 中断使用当前线程栈，没有独立中断栈；
- CLIC 不做优先级抢占；
- Cache 已打开，但还没有 DMA/显示所需的 clean/invalidate API；
- 没有 MPU/PMU、低功耗、second core；
- 没有从 Bootloader 读取启动参数；
- 没有 PSRAM 运行时容量探测。
