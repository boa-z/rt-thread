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

当前所有 PLIC 中断优先级为 1，没有抢占分级。D21x PLIC 支持优先级，但 BSP 尚未使用。显示 VSYNC、以太网、USB 和音频播放 DMA 周期中断均已接入，目前与其它中断同为优先级 1；若后续做抢占分级，需要重新设计优先级，避免 UART/CAN 被高带宽中断长期压制。

## 6.6 DMA 现状

本 BSP 有三类 DMA：

1. **控制器内部 DMA**：SDMC 的 IDMA、DE/GE 的 AXI 读写。数据面由控制器
   自己搬移，软件只准备描述符和缓冲区；
2. **通用 DMA 缓冲管理**：`../libraries/soc/d21x/drivers/drv_dma.c` 提供跨驱动复用的分配、
   对齐和 Cache 维护接口，供 SDMC、UART 和音频使用；
3. **外部 DMA 控制器（DMAC）**：8 个共享通道，供 UART/SPI/I2S 等外设
   使用，已完成移植、驱动与实机自检，见 6.6.1。

`../libraries/soc/d21x/drivers/drv_dma.c` 的接口：

```c
void *aic_dma_alloc(rt_size_t size);       /* 64 B 对齐，尾部补整行 */
void  aic_dma_free(void *ptr);
rt_bool_t aic_dma_range_is_aligned(const void *addr, rt_size_t size);
void  aic_dma_cache_clean(const void *addr, rt_size_t size);
void  aic_dma_cache_invalidate(const void *addr, rt_size_t size);
void  aic_dma_cache_clean_invalidate(const void *addr, rt_size_t size);
```

D213 没有 MMU，CPU 地址就是 DMA 地址，因此不需要 IOMMU 映射；需要处理
的只有：

- 缓冲区和描述符按 64 B Cache 行对齐；
- CPU→设备方向 `clean`；
- 设备→CPU 方向在 DMA 前后 `invalidate`；
- 描述符数组在启动 DMA 前 `clean`，轮询完成位前 `invalidate`。

SDMC 的实际用法：

- 大块传输和非 32 位对齐的 SDIO CMD53 走 IDMA；
- 调用方缓冲区始终先复制到 `aic_dma_alloc()` 的 bounce buffer；
- 每个 4 KiB 描述符，512 块 × 512 B 最多 64 个描述符；
- `DAT_DONE` 只表示卡的数据相位结束，IDMAC 可能仍在清 FIFO；驱动继续
  轮询最后一个描述符的 `OWN` 位，确认 DMA 真正完成后才把数据交给 CPU；
- IDMA 连续失败 3 次会自动退回 CPU FIFO，可用 `sd_dma 1` 重新打开。

其余外设：

- UART TX 已接入外部 DMAC（`BSP_USING_UART_DMA`）；RX 为 1 ms 轮询；
- 音频播放已接入外部 DMAC（`BSP_USING_AUDIO`，循环传输 + 周期中断）；
- I2C 使用 HAL 轮询；
- CAN 使用控制器内部缓冲；
- PWM/WDT/RTC 不需要 DMA；
- USB Host 与以太网已实现并接入；USB Device/OTG 尚未实现。

### 6.6.1 外部 DMA 控制器（DMAC）

除控制器内部 DMA 外，D21x 还有一个 8 通道的共享 DMA 控制器
（`0x10000000`，PLIC IRQ 32）。本 BSP 已移植 DMA v1.0 的 HAL，作为
UART/SPI/I2S 等高速外设的公共基础：

| 组成 | 说明 |
|---|---|
| `../libraries/soc/d21x/hal/hal_dma.c` | 任务描述符、通道申请/释放、memcpy/memset/设备/循环传输、IRQ |
| `../libraries/soc/d21x/include/hal_dma.h` | 对外接口与传输方向/位宽/burst 枚举 |
| `../libraries/soc/d21x/include/hal_dma_reg.h` | DMA v1.0 寄存器、DRQ 端口表、36 个任务描述符 |
| `../libraries/soc/d21x/include/aic_dma_id.h` | D21x DRQ 编号（UART0-7、SPI0-3、I2S0/1、PSADC 等） |
| `../libraries/soc/d21x/drivers/drv_dmac.c` | 时钟/复位、IRQ 安装、`dma_test`/`dma_dump`、共享自检 API |
| `../libraries/soc/d21x/drivers/drv_dmac.h` | `aic_dmac_ready()`、`aic_dmac_memcpy_test()`（MSH 与 utest 共用） |

接口（与 Luban-Lite 一致，便于对照原厂代码）：

```c
struct aic_dma_chan *hal_request_dma_chan(void);
int hal_release_dma_chan(struct aic_dma_chan *chan);
int hal_dma_chan_config(struct aic_dma_chan *chan,
                        struct dma_slave_config *cfg);
int hal_dma_chan_prep_memcpy(struct aic_dma_chan *, u32 dst, u32 src, u32 len);
int hal_dma_chan_prep_memset(struct aic_dma_chan *, u32 dst, u32 value, u32 len);
int hal_dma_chan_prep_device(struct aic_dma_chan *, u32 dst, u32 src,
                             u32 len, enum dma_transfer_direction dir);
int hal_dma_chan_prep_cyclic(struct aic_dma_chan *, u32 buf, u32 buf_len,
                             u32 period_len, enum dma_transfer_direction dir);
int hal_dma_chan_start(struct aic_dma_chan *chan);
int hal_dma_chan_stop(struct aic_dma_chan *chan);
int hal_dma_chan_register_cb(struct aic_dma_chan *, dma_async_callback, void *);
```

要点：

- **描述符链**：每次传输由一个或多个 64 B 对齐的 `aic_dma_task` 组成，
  控制器按 `p_next` 自行遍历；`hal_dma_chan_start()` 在启动前 clean
  描述符，避免控制器读到旧 Cache 行；
- **完成通知**：非循环传输使用 `DMA_IRQ_ALL_TASK`，完成时进 PLIC IRQ 32，
  `drv_dmac.c` 的 ISR 转给 `hal_dma_irq()`，回调在中断上下文执行；
- **Cache**：HAL 在准备阶段 clean 源、clean+invalidate 目的；设备→内存
  方向完成后，CPU 仍需自行 `invalidate` 目的范围（`dma_test` 已这样做）；
- **时钟/复位**：`CLK_DMA` 的 CMU 寄存器 `0x0410`，bus gate bit 12、
  RSTN bit 13，无独立 module gate；
- **内存分配**：源/目的必须是 8 B 对齐；测试和后续用户应使用
  `aic_dma_alloc()`（64 B 对齐）。

配置与验证：

```text
CONFIG_BSP_USING_DMAC=y      # 默认 n，不改变现有镜像
configs/d213-dma.config      # minimal + DMAC + dma_test
configs/d213-dma-utest.config # utest + DMAC（bsp.d213.dmac）
```

```text
msh > dma_test 4096          # 内存到内存复制 + Cache 校验
msh > dma_test 1048576       # 1 MiB，吞吐有 1 ms tick 分辨率
msh > dma_dump 0             # 打印通道寄存器和描述符
```

`dma_test` 会申请通道、复制缓冲区、等待完成中断、invalidate 后逐字节
比较并打印吞吐。

当前证据级别：**HW PASS**（2026-11-07 OS-only HIL，build id
`...-5d9a9578`；2026-11-08 复测 7/7，HIL `20261108-020441-21088`；
共享层重构后复测 7/7，HIL `20261109-093211-17328`）：

- `bsp.d213.dmac` 7/7 PASS：4 KiB、64 KiB、拒绝 4 B、连续 8 次 8 KiB，
  以及通道全部占用/释放/复用、循环传输非法参数拒绝；
- `dma_test 1048576` PASS，170666 KB/s；4 KiB/64 KiB 拷贝均小于 1 ms
  tick，因此打印 `<1 ms` 而不是吞吐；
- 通道号从 0 开始按请求顺序分配，`dma_dump` 在释放后读到清零的通道
  寄存器属正常现象。

DMAC 已接入 UART TX（下面 6.6.2）；SPI/I2S 仍需单独接入和实机验证。
另外，原厂 `GET_DMA_DST_BURST()` 宏的位域写法（先左移再取低 2 位）
实际恒为 0，本移植保持原样，避免在没有明确证据时“修正”厂商时序。

### 6.6.2 UART TX DMA

`BSP_USING_UART_DMA`（依赖 `BSP_USING_DMAC`）把 UART 的写请求交给 DMAC：

- 通道按需申请，DRQ = `DMA_ID_UART0 + n`；完成后在 DMAC 回调里释放；
- 数据经每实例的 8 B 对齐暂存区分片（256 B/片）传输，调用方缓冲区无需
  对齐、长度不限；
- 传输中不复位 TX/RX FIFO：DMAC 完成只代表数据进入 UART FIFO，复位会
  丢掉未发出的字节并破坏并发接收；
- 未对齐由暂存区解决；通道忙或无空闲通道时回退轮询发送，写不丢失；
- 完成回调调用 `rt_hw_serial_isr(RT_SERIAL_EVENT_TX_DMADONE)`，串口框架的
  DMA TX 队列据此推进；剩余分片在回调里直接续传；
- 关闭设备会中止在途传输，重配置在传输期间返回 `-RT_EBUSY`；守护线程
  5 s 超时中止并推进队列，避免永久卡死。

```text
CONFIG_BSP_USING_DMAC=y
CONFIG_RT_SERIAL_USING_DMA=y
CONFIG_BSP_USING_UART_DMA=y
configs/d213-uart-dma-utest.config  # utest + DMAC + UART1 DMA
```

```text
msh > uart_dma_test 4096   # MCR 内部回环，无需外部连线
msh > utest_run bsp.d213.uart1.dma
```

证据级别：**HW PASS**（2026-11-07 单包 4/4，HIL
`20261107-205624-5680`；2026-11-08 扩展后 9/9，HIL
`20261108-020309-21624`；共享层重构后复测 9/9，
HIL `20261109-093337-15756`）：覆盖 64 B、1 KiB、4 KiB、4 B 拒绝、8×513 B
连续分片、8 KiB 对齐分片、8 通道全占用轮询回退、TX DMA 与标准 `INT_RX`
并发、传输中关闭后重传。4 KiB 吞吐 11 KB/s，受 115200 波特率限制。
注意循环回环测试的收端直接读硬件 FIFO：若同时以 `INT_RX` 打开设备，1 ms
RX 轮询线程可能在刷新与发送之间把空闲线字节放进 ring，造成首字节错位。
UART1..7 的时钟分频修复见 07 章 7.3.1。

## 6.7 Cache 现状

`board/cache.c` 提供 C906 D-Cache range 操作，`../libraries/soc/d21x/drivers/drv_dma.c` 在其上
提供统一的对齐分配接口。使用方式：

- CPU 写完 Framebuffer、交给 DE 扫描前：`clean`；
- CPU 写 GE 源/目标前：`clean_invalidate`，避免旧脏行回写覆盖 GE 结果；
- GE 完成后 CPU 读目标前：`invalidate`；
- SDMC IDMA 读卡：DMA 前后都 `invalidate` bounce buffer；写卡前 `clean`；
- SDMC 描述符：启动前 `clean`，轮询 `OWN` 前 `invalidate`；
- 所有 DMA 缓冲区优先使用 `aic_dma_alloc()`，保证首尾都落在 64 B
  Cache 行边界内。

这些操作不会关闭 Cache。仍需要注意：

1. D-Cache 当前是否开启由 Bootloader/CPU 状态决定；
2. 直接使用 `rt_malloc()` 得到的缓冲区不能假设满足 DMA 对齐，必须
   使用 `aic_dma_alloc()` 或自行处理 bounce；
3. 64 字节对齐的调用方缓冲区可以零拷贝，但当前 SDMC 为简化错误恢复
   始终使用 bounce buffer；
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
