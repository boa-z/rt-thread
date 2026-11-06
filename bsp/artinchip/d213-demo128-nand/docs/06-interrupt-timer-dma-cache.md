# 06 中断、Timer、DMA 与 Cache

本 BSP 使用 RT-Thread Next RISC-V common port 的中断/Timer 框架。

## 6.1 中断注册

外设驱动使用 common `rt_hw_interrupt_install()`，并且必须把硬件 PLIC
IRQ 号转换成框架 IRQ 号：

```c
rt_hw_interrupt_install(IRQ_PLIC(CAN0_IRQn), aic_can_isr, &handle, "can0");
rt_hw_interrupt_umask(IRQ_PLIC(CAN0_IRQn));
```

`IRQ_PLIC(hwirq)` 等于 `irq_plic_base + hwirq`。D213 的
`irq_plic_base` 由 common local INTC 数量决定，不能假设等于 0。

`board/interrupt.c` 只提供：

```c
platform_get_plic_base();   /* 0x20000000 */
platform_get_plic_size();   /* 0x210000 */
platform_get_plic_quirks(); /* 0 */
aic_plic_quirk_init();      /* D21x PER=1 */
```

`irq_desc[]`、mask/umask、claim/complete 都由
`libcpu/risc-v/common/interrupt.c` 和 `pic-sifive-plic.c` 负责。

## 6.2 中断上下文约束

ISR 中不能：

- 调用 `rt_thread_mdelay()`、`rt_sem_take(..., RT_WAITING_FOREVER)`；
- 调用可能睡眠的 I2C/SPI/块设备接口；
- 做长时间忙等；
- 调用 `rt_malloc()`（当前堆实现可在中断中工作，但不建议）。

可以：

- 读/写 MMIO；
- `rt_sem_release()`、`rt_completion_done()`；
- `rt_hw_can_isr()`、`rt_hw_serial_isr()` 等框架 ISR；
- 记录统计量。

## 6.3 PLIC 细节

D21x 的 PLIC 与标准 SiFive PLIC 的 priority/enable/context 偏移兼容：

```text
priority      +0x000000
enable        +0x002000
M-mode context +0x200000
  threshold   +0x0
  claim       +0x4
```

额外需要一个 vendor PER 位：

```c
*(volatile uint32_t *)(AIC_PLIC_BASE + 0x01FFFFC) = 1;
```

`aic_plic_quirk_init()` 在 `rt_hw_interrupt_init()` 之后调用。不要删除
这一步，否则 D21x PLIC 的优先级功能可能不生效。

## 6.4 Tick 与延时

common `riscv_timer.c` 负责 Tick 和 `rt_hw_us_delay()`。D213 只覆盖：

```c
rt_uint64_t riscv_timer_rdtime(void);                 /* rdtime CSR */
void riscv_timer_set_compare(rt_uint64_t value);      /* 0x24004000 */
rt_uint64_t rt_hw_get_clock_timer_freq(void);         /* 4 MHz */
```

`RT_TICK_PER_SECOND = 1000`，tick 间隔 4000 个 CLINT 周期。common
timer 从“当前时间 + 间隔”重新装载；如果 ISR 延迟过大，会从当前时间
重新起步，避免连续追赶造成中断风暴。

`rt_hw_us_delay()` 是忙等，适合寄存器时序，不适合长延时；长延时用
`rt_thread_mdelay()`。

## 6.5 中断性能与优先级

当前所有 PLIC 中断优先级为 1，没有抢占分级。D21x PLIC 支持优先级，但 BSP 尚未使用。若后续接入：

- 显示 VSYNC；
- 以太网；
- USB；
- 音频 DMA；

需要重新设计优先级，避免 UART/CAN 被高带宽中断长期压制。

## 6.6 DMA 现状

**当前 BSP 没有通用 DMA 驱动。** 具体含义：

- UART 使用 CPU 轮询发送 + RX FIFO 中断；
- I2C 使用 HAL 轮询；
- CAN 使用控制器内部缓冲；
- PWM/WDT/RTC 不需要 DMA；
- 显示 DE/GE 使用控制器自身的 AXI 读写，不需要软件 DMA 描述符；
- 存储、音频、网络的 DMA 尚未实现。

DE/GE 直接读写 DDR，因此 `board/cache.c` 提供 C906 D-Cache range clean/invalidate，供 Framebuffer 和 GE 缓冲区在 CPU/控制器所有权切换时使用。

原厂 `hal/dma` 和 `drv/dma` 没有复制到本 BSP，因为：

1. 当前外设不需要；
2. DMA 会引入 Cache 一致性、描述符对齐、通道仲裁和中断映射问题；
3. 没有实机条件时无法验证。

后续接入 DMA 时必须完成：

- 通道申请/释放；
- 描述符和缓冲区的物理地址转换（无 MMU，直接用 `uintptr_t`）；
- Cache clean/invalidate；
- 传输完成/错误中断；
- 超时和 abort；
- 与 RT-Thread 设备框架的同步。

## 6.7 Cache 现状

显示/GE 已经使用 `board/cache.c` 的 C906 D-Cache range 操作：

- CPU 写完 Framebuffer、交给 DE 扫描前：`clean`；
- CPU 写 GE 源/目标前：`clean_invalidate`，避免旧脏行回写覆盖 GE 结果；
- GE 完成后 CPU 读目标前：`invalidate`；
- 缓冲区按 64 字节 Cache 行对齐（Framebuffer 由链接脚本对齐，GE 测试缓冲用 `rt_malloc_align(..., 64)`）。

这些操作不会关闭 Cache。仍需要注意：

1. D-Cache 当前是否开启由 Bootloader/CPU 状态决定；
2. 只有显示/GE 路径做了显式维护，通用 DMA 尚未接入；
3. 非 64 字节对齐的 GE 缓冲区会被范围函数扩展到完整 Cache 行，可能触及相邻数据；
4. 多核共享缓冲区不在本 BSP 范围内。

不要用“全局关闭 Cache”掩盖一致性问题。

## 6.8 常见中断问题

| 现象 | 可能原因 | 排查 |
|---|---|---|
| `Unhandled IRQ 88` | CAN ISR 未注册/被 mask | 检查 `rt_hw_interrupt_install` 和 `umask` |
| 中断只进一次 | 外设中断标志未清 | 读外设 status/clear 寄存器 |
| 系统卡死 | ISR 中睡眠/死循环 | 检查 ISR 调用链 |
| Tick 变慢 | `mtimecmp` 设置错误/中断被长期关闭 | 检查 `mie`、临界区 |
| GPIO 中断风暴 | 电平触发且未清 | 改为边沿或清 status |
| CAN 不进 RX | 未 `RT_DEVICE_CTRL_SET_INT` 或过滤器错误 | 检查 open flag 和过滤器 |

## 6.9 中断调试示例

```c
/* 在 ISR 中只做最小工作 */
static void example_isr(int irq, void *param)
{
    struct example *dev = param;
    uint32_t status = readl(dev->base + REG_STATUS);

    writel(status, dev->base + REG_STATUS);   /* W1C */
    if (status & REG_RX_READY)
        rt_sem_release(&dev->rx_sem);         /* 唤醒线程 */
}
```

线程侧：

```c
rt_sem_take(&dev->rx_sem, RT_WAITING_FOREVER);
/* 在线程上下文读数据、解析协议 */
```
