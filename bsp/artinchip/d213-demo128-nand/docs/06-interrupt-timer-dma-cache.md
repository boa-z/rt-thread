# 06 中断、Timer、DMA 与 Cache

## 6.1 中断注册

BSP 使用 RT-Thread 的 `rt_hw_interrupt_install()` 注册 PLIC 中断：

```c
rt_hw_interrupt_install(CAN0_IRQn, aic_can_isr, &handle, "can0");
rt_hw_interrupt_umask(CAN0_IRQn);
```

`board/interrupt.c` 维护 `aic_irq_table[128]`：

```mermaid
flowchart LR
    A[PLIC claim] --> B[irq number]
    B --> C[aic_irq_table[irq].handler]
    C --> D[ISR]
    D --> E[PLIC complete]
```

- 未注册的中断打印 `Unhandled IRQ n`；
- `rt_hw_interrupt_mask()` 关闭 PLIC enable 位；
- `rt_hw_interrupt_umask()` 打开 PLIC enable 位；
- 中断优先级默认 1，阈值 0。

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

D21x 的 PLIC 是 T-Head/C906 兼容布局，但与标准 SiFive PLIC 的 enable/claim 偏移不同。当前实现：

```c
#define AIC_PLIC_PRIO_BASE   (AIC_PLIC_BASE + 0x0000000)
#define AIC_PLIC_ENABLE_BASE (AIC_PLIC_BASE + 0x0002000)
#define AIC_PLIC_PER         (AIC_PLIC_BASE + 0x01FFFFC)
#define AIC_PLIC_THRESHOLD   (AIC_PLIC_BASE + 0x0200000)
#define AIC_PLIC_CLAIM       (AIC_PLIC_BASE + 0x0200004)
```

如果移植到其他 D21x 型号，必须核对 PLIC 布局，不能直接假设与 D213 相同。

## 6.4 Tick 与延时

### Tick

```c
aic_tick_interval = AIC_CLINT_FREQ / RT_TICK_PER_SECOND; // 4 MHz / 1000 = 4000
aic_next_tick = aic_read_time() + aic_tick_interval;
aic_set_mtimecmp(aic_next_tick);
```

中断处理：

```c
rt_tick_increase();
aic_next_tick += aic_tick_interval;
if (aic_next_tick <= now)
    aic_next_tick = now + aic_tick_interval;
aic_set_mtimecmp(aic_next_tick);
```

优点：不会把 ISR 延迟累积成 Tick 漂移。

### 微秒延时

```c
void rt_hw_us_delay(rt_uint32_t us)
{
    uint64_t start = aic_read_time();
    uint64_t ticks = (uint64_t)us * (AIC_CLINT_FREQ / 1000000U);
    while ((aic_read_time() - start) < ticks);
}
```

这是忙等，适合寄存器时序，不适合长延时。长延时用 `rt_thread_mdelay()`。

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
