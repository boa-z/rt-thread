# D12p/D126 架构与移植说明

本 BSP 使用 RT-Thread 官方 Next RISC-V common port（PR #10320）。
D126 不再维护 BSP 内 `cpu/` 端口。

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

两者共用 `libcpu/risc-v/common` 的启动、Trap、上下文切换、线程栈、
IRQ 栈和 Timer 核心；SoC 差异由 BSP 的 platform hook 和 timer
accessor 提供。

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

链接脚本：

```text
0x40000000  .text / .rodata / .data / .bss
            .percpu
            heap -> 0x40800000
```

## 3. 启动链

```text
BROM
  -> PBP（PSRAM 初始化）
  -> SPL / bootloader
  -> d12p_os.itb
  -> common _start
  -> rtthread_startup
```

common startup 负责：

- 读取 `mhartid`；
- 设置 `gp`、`.percpu` 早期栈；
- 调用 `riscv_early_setup()`（weak，BSP 覆盖）；
- 清 `mie/mip`、`mscratch`；
- 清 BSS；
- `riscv_boot_hart_init()`；
- 初始化 IRQ 栈；
- 设置 `mtvec = system_vectors`。

BSP 不提供 `startup_gcc.S`，也不复制 common startup。

### E907 MEXSTATUS 和 IRQ 栈

原厂 SPL 的 `SystemInit()` 会设置 E907 扩展 CSR `MEXSTATUS`
（`0x7e1`）的 bit16 `SPUSHEN` 和 bit17 `SPSWAPEN`，这两个位在跳入
应用后仍然保留：

- `SPUSHEN`：中断入口允许推测执行 `IPUSH`；
- `SPSWAPEN`：level-0 中断进入/返回时由硬件交换 `sp`/`mscratch`。

common port 用软件方式切换 IRQ 栈（`system_vectors` 中
`csrrw sp, mscratch, sp` 后把 `mscratch` 清 0），与 `SPSWAPEN` 的硬件
交换叠加后，异常帧会以 IRQ 栈顶作为被打断的 `sp`，随后被 C trap
handler 覆盖，首次 tick 中断即触发嵌套的 store access fault
（`mcause=0x30000007`、`MPIE=0`）。

本 BSP 在 `board/board.c` 覆盖 `riscv_early_setup()`，在 BSS 清零
之前清除 `SPUSHEN`/`SPSWAPEN`，保留 `MEXSTATUS` 其余位，并记录
boot 值供启动日志打印。common port 不做修改。

## 4. CLIC

D12p 的 CLIC 由 common `pic-riscv-clic.c` 驱动，使用 **direct mode**：

- `mtvec = system_vectors`；
- `mcause` 直接给出 IRQ 号；
- `trap.c` 调用 `rt_hw_interrupt_handle()`；
- `rt_hw_interrupt_install()` / mask / umask 由 common 提供；
- BSP 只提供：

```c
platform_get_clic_base()   /* 0x20800000 */
platform_get_clic_size()   /* 0x2000 */
platform_get_clic_quirks() /* MINTTHRESH quirk */
```

CLIC 寄存器布局：

```text
CLICCFG      +0x0000
CLICINFO     +0x0004
MINTTHRESH   +0x0008
CLICINT[n]   +0x1000 + n*4
```

风险：如果实物只支持 SHV/vector mode，需要增加
`RISCV_CLIC_QUIRK_SHV` 和硬件 vector table。当前未使用 SHV。

## 5. 上下文切换和 FPU

common `context_gcc.S` 负责：

- 保存/恢复 s0-s11、ra、tp、xstatus；
- 直接线程切换和中断切换使用同一套 common 实现。

中断 trap 路径的 `SAVE_ALL`/`RESTORE_ALL`（`asm.h`）在
`ARCH_RISCV_FPU` 下保存/恢复 `f0`-`f31` 和 `fcsr`；线程切换路径的
`SAVE_CONTEXT`/`RESTORE_CONTEXT` 目前不包含浮点寄存器。因此跨线程的
FPU 上下文尚不正确，实机 `d126_selftest` 的两个 FPU worker 都会
FAIL（实测编译结果把中间值放在 `fs1` 并跨 `rt_thread_mdelay` 使用）。
该问题需要 common port 的 FPU 上下文修复，不在 D126 BSP 内解决。

D126 没有 `PendSV_Handler`、`__Vectors` 或 TSPEND 自维护代码。

`rt_hw_stack_init()` 为新线程设置 `K_XSTATUS_DEFAULT`（M-mode、
MPIE、FS），但线程切换帧本身没有 FPU 区域。

## 6. Timer

common `riscv_timer.c` 负责 tick。D12p 覆盖三个 weak accessor：

```c
riscv_timer_rdtime()        /* CORET MTIME: +0x7ff8/+0x7ffc */
riscv_timer_set_compare()   /* CORET MTIMECMP: +0x0000/+0x0004 */
rt_hw_get_clock_timer_freq()/* BSP_CLINT_FREQ (4 MHz) */
```

MTIME 是 64 位计数器。原厂头文件注释写成 `+0x7ffc`，但 64 位成员按
8 字节对齐，低字实际在 `+0x7ff8`、高字在 `+0x7ffc`；原厂 SPL 和
Luban-Lite RT-Thread 镜像的反汇编都以 `+0x7ff8/+0x7ffc` 访问。
按注释取址会读到高字和被清零/保留的字，`rdtime` 返回错误值，
`MTIMECMP` 永远小于 MTIME，电平触发的 MTIP 无法撤销，首个 tick
之后 CPU 会停在中断循环里。

`RT_TICK_PER_SECOND = 1000`。`rt_hw_board_init()` 调用
`riscv_timer_set_frequency(aic_cpu_clock())` 和 `rt_hw_tick_init()`，
然后 `rt_hw_interrupt_umask(7)` 使能 CLICINTIE[MTIP]。

## 7. 时钟和 UART

`clock.c` 不重新配置 PLL，只：

1. 从 PLL_INT0/INT1 的 GEN 寄存器推导频率；
2. 为 UART0 选择分频，使模块时钟接近 24 MHz；
3. 打开 UART0 gate，解除复位；
4. 由模块时钟计算 115200 分频。

UART0 引脚按原厂 PBP 配置：PA0=TX、PA1=RX，pinmux 值 `0x325`。
控制台是自定义 char 设备 + 轮询；后续应改为标准
`rt_serial_device` + 中断 RX。

## 8. 与 D213 的边界

共用：

- `libcpu/risc-v/common` 启动、Trap、上下文切换、Timer 核心；
- RT-Thread 设备框架、pin 框架、FinSH、内存管理；
- 共享 ELF/build 检查工具。

不共用：

- PLIC vs CLIC；
- CLINT `rdtime` vs CORET MMIO；
- D213 C906 Cache vs D12p 32 字节 cache；
- DDR vs PSRAM 链接布局；
- LVDS vs MIPI DSI；
- Classic CAN vs CAN FD。

## 9. 当前限制

- CLIC direct mode 的中断入口、tick、线程调度和中断返回已在实机
  验证（msh、shell 连续命令稳定）；
- CLIC `CLICINFO` 实机为 128 IRQs / 3 control bits；
- PSRAM 实测为 D126FCR 16 MB（eFuse `mark_id=0x06`、`psram_id=0x02`，
  `PSRAM ID=0xdd8ddd8d`）；应用 heap 仍按 8 MB 保守配置；
- `MEXSTATUS` 实机 boot 值 `0x00030010`，BSP 初始化后为
  `0x00000010`（bit4 为原厂复位值，保留）；
- FPU 跨线程上下文仍 FAIL，依赖 common port 的未合并修复；
- 控制台 RX 是阻塞轮询；
- 没有 I2C/SPI/WDT/RTC/PWM/CAN FD/显示/触摸；
- 没有 DMA/cache API。
