# 测试与实机验证清单

## 1. 测试分级

| 级别 | 含义 | 入口 | 当前状态 |
|---|---|---|---|
| BUILD | 交叉编译、链接、ELF 检查 | `../tools/build-check.py` | PASS |
| HOST | 打包脚本、HIL 主机单元测试 | `../tools/test_d126_hil.py` | PASS |
| PACKAGE | ITB/IMG 生成（真实 bootloader） | `build-check.py` 的 `package` 阶段 | PASS |
| HIL（只读） | 串口自动识别、测试、报告 | `d126_hil.py --mode serial` | HW PASS（2026-11-10） |
| HIL（闭环） | 构建→打包→OS-only 升级→Build ID→utest | `d126_hil.py --mode full` | HW PASS（2026-11-10） |
| HIL（复位/长稳） | 有界热复位 + 定时长稳 + 堆漂移 | `d126_hil.py --mode stability` | HW PASS（2026-11-10） |

只有 BUILD/HOST/PACKAGE 通过，不能说明固件可以启动。BUILD、PACKAGE
和 HIL 实机结果必须分别记录。

## 2. 构建检查

推荐使用统一矩阵（自动备份/恢复 `.config`，禁止手改 `rtconfig.h`）：

```bash
cd bsp/artinchip
python tools/build-check.py --targets d126-minimal \
    --sdk /path/to/luban-lite-jc
python tools/build-check.py --targets d126-utest \
    --sdk /path/to/luban-lite-jc
```

结果和 manifest 写入 `bsp/artinchip/artifacts/<target>/`；HIL 只使用
manifest 里的镜像和 SHA256，不按 mtime 猜。

直接 `scons` 时：

```bash
cd bsp/artinchip/d126fxr-demo-v1-1
scons -j8
python ../tools/check_elf.py --elf rtthread.elf --map rtthread.map \
    --xlen 32 --entry 0x40000000 --intc clic
```

预期：

```text
ArtInChip RISC-V check: PASS
  ELF          : ELF32, single-float ABI
  entry/_start : 0x40000000
  trap vector  : 0x40000180 (direct mode)
  controler    : CLIC
```

地址会随代码/配置变化，但 entry 必须为 `0x40000000`，
`system_vectors` 必须 64 字节对齐，且不应出现旧的 `__Vectors`/
`PendSV_Handler`。

## 3. 打包检查

`build-check.py` 会调用 `tools/mk_firmware.py` 生成 ITB 和最终镜像。
镜像必须包含真实 bootloader（来自同一棵 Luban-Lite 的
`output/d12p_d126-demo124-nor_*/images/bootloader.bin`）。占位
bootloader 镜像会被改名为 `*_PLACEHOLDER_DO_NOT_FLASH.img`，**不可
烧录**。

本地校验镜像：

```bash
artinchip-flash info artifacts/d126-utest/images/d12p_d126-demo124-nor_v1.0.0.img
artinchip-flash burn artifacts/d126-utest/images/d12p_d126-demo124-nor_v1.0.0.img \
    --partition os --plan
```

`info` 必须显示 `Product: d126-demo124-nor`。
`--plan` 必须只对 `image.target.os` 显示 `selected=true`，其余
`image.target.spl/env/env_r` 为 `selected=false`。这两条检查都不访问
USB 设备。

## 4. 实机启动测试

连接 UART0（115200 8N1，PA0=TX，PA1=RX），记录完整日志，至少包含：

1. BROM/PBP 版本和 PSRAM 初始化日志；
2. SPL/bootloader 加载 `d12p_os.itb` 的日志；
3. RT-Thread 版本和 BSP banner；
4. `main: running`；
5. `aic_build_id` 输出的 Build ID；
6. `msh >` 提示符；
7. `uart0` 已注册为 `rt_serial_device`，RX 由 IRQ 78 中断驱动：
   输入的每条 msh 命令都能回显并执行。

```text
msh > d126_info
d126_info: board=d126-demo124-nor cpu=e907f psram=8MB tick_hz=1000 uptime_s=...
msh > aic_build_id
build_id: d126fxr-demo-v1-1-<git>-<cfg>-<time>
...
```

如果 UART 输出乱码，先确认 PLL_INT1 和 UART0 分频，再检查
`clock.c` 的 PLL 读取公式和 PA0/PA1 pinmux。如果完全没有输出，检查
bootloader 是否跳转到 `0x40000000`，并确认 PSRAM 初始化和容量。

## 5. Tick / 调度 / IPC 测试

本阶段 FPU 完全不在范围内：不运行 FPU 测试、不修改 FPU 代码，也不
把 FPU 结果计入验收。启动时不再自动执行 `d126_selftest`；该命令保留
供未来专项验证。

```text
msh > d126_tick 100
d126_tick: delay=100 tick=100 expected=100 PASS
msh > d126_sched 1000
d126_sched: loops=1000 counter=2000 expected=2000 PASS
msh > d126_ipc
d126_ipc: sem=PASS mb=PASS PASS
```

判定：

- `d126_tick` 的 `tick` 必须在 `expected` 下界（容差 2 tick）与
  `expected + expected/4 + 2` 之间；明显偏离说明 CORET 频率或
  `MTIMECMP` 配置错误；
- `d126_sched` 完成后 `counter` 必须等于 `loops * 2`，超时 10 s；
- `d126_ipc` 的 semaphore 和 mailbox 都必须 PASS；
- 以上任一命令卡死、复位或超时都记为 FAIL，不得记为 SKIP。

`utest_run bsp.d126.tick` 对 1/2/5/10/50/100/200 ms 逐个复用同一判定：

```text
d126_tick: delay=1 tick=1 expected=1 PASS
...
d126_tick: delay=200 tick=200 expected=200 PASS
```

实机结果：7 个间隔全部 `tick == expected`（2026-11-10），说明 4 MHz
CORET 分频到 1 kHz 的比例和 `MTIMECMP` 更新路径没有累计偏差。

`utest_run bsp.d126.scheduler` 的抢占用例：优先级 8 的线程阻塞在
semaphore 上，优先级 24 的线程释放该 semaphore 后，高优先级线程必须
在低优先级线程继续执行前运行；用执行顺序计数验证，超时 500 ms。

## 6. PSRAM / heap 测试

```text
msh > d126_memtest 1024
memtest: 1024 KiB @ 0x...: PASS
```

建议再测 `d126_memtest 4096`。边界：

- 默认 heap 约 8 MB；
- 如果实际是 16 MB PSRAM，当前 BSP 只使用前 8 MB；
- 不要用 `d126_memtest` 测试 heap 之外的地址，可能踩到未初始化
  PSRAM。

`utest_run bsp.d126.memory` 另外覆盖 heap 边界和碎片压力：

- `tc_psram_boundary` 先取 4 MiB、再取 3 MiB，校验块地址和长度都在
  `[0x40000000, 0x40000000 + 8 MB)` 内，对 4 MiB 块做全量图案走线，
  最后请求整个 heap 大小并必须返回 `NULL`，且 `used` 不变；
- `tc_heap_stress` 做 8 轮 4–32 KiB 多块分配、写入、校验、释放，每轮
  结束 `used` 必须等于基线（无泄漏/漂移）。

```text
msh > utest_run -thread bsp.d126.memory
[  PASSED  ] [ result   ] testcase (bsp.d126.memory)
```

实机结果：PASS（1.55 s，2026-11-10，Build ID
`d126-demo124-nor-e20665c12c02-dirty-84ce693a-2026-11-10T10:16:36Z`）。

## 7. utest

`d126-utest` 配置包含 `components.utilities.utest.uassert`、`core.irq`、
`core.semaphore`、`core.mutex`、`core.mutex_pi` 和 `bsp.d126.*`
（tick/scheduler/ipc/memory/gpio）。FPU 用例不在配置内。

`bsp.d126.gpio` 的中断用例同时验证 CLIC 行为：PB12 输出自环产生真实
边沿；在 `rt_hw_interrupt_disable()` 期间驱动边沿时回调不得执行，
`rt_hw_interrupt_enable()` 后挂起的边沿必须送达；`detach` 后再次翻转
不得触发回调。

```bash
python ../tools/d126_utest.py --port COM10
```

结果分类：PASS / FAIL / KNOWN-FAIL / TIMEOUT / RESET。脚本只读串口，
不烧录、不复位。

D213 回归若需要显式排除 FPU 用例，使用 `--exclude`，不要修改原有
测试结果或 known-fail 列表：

```bash
python ../tools/d213_utest.py --port COM7 --exclude 'bsp.d213.fpu.*'
```

## 8. HIL 自动化

所有模式默认先做本地安全检查；`plan` 不打开串口、不探测 USB、不改变
板级状态。

```bash
cd bsp/artinchip
python tools/d126_hil.py --mode plan
python tools/d126_hil.py --mode serial --port COM10 --repeat 3
python tools/d126_hil.py --mode full --port COM10 \
    --sdk C:/Project/D50T-2-Lite/luban-lite-jc \
    --confirm-board d126-demo124-nor --flash-scope os
python tools/d126_hil.py --mode stability --port COM10 \
    --confirm-board d126-demo124-nor --cycles 5 --duration 120
```

安全约束：

- `--mode full` 必须显式 `--confirm-board d126-demo124-nor`；
- 默认只写 `image.target.os`；`--flash-scope full` 必须再加
  `--confirm-full-flash`；
- 禁止 `flasherase`、eFuse、安全启动和未知 NAND/NOR 区域操作；
- 串口和 USB 目标使用跨进程文件锁；构建前后校验源码树状态；
- 镜像必须与 manifest SHA256 一致，`Product` 必须是
  `d126-demo124-nor`，OS-only 计划必须只选中 `image.target.os`；
- 刷写次数有上限，utest 失败不会触发重复刷写；
- 报告写入 `artifacts/d126-hil/<run_id>/report.json`，包含 Git SHA、
  Build ID、镜像 SHA256、各阶段状态和原始串口日志。

### 8.1 热复位与长稳

`--mode stability` 只通过串口复位和测试，不构建、不刷写。它要求显式
`--confirm-board d126-demo124-nor`，次数上限 20，时长上限 3600 s。

```bash
python tools/d126_hil.py --mode stability --port COM10 \
    --confirm-board d126-demo124-nor --cycles 5 --duration 120
```

每个复位周期的判定：

1. 发送 `reboot`，必须看到 `reboot:` 确认后 BSP banner；
2. 在 `--boot-timeout` 内出现新的 `msh >`，否则记为
   `BOOT_TIMEOUT`/`NO_PROMPT` 并立即停止；
3. 校验 `d126_info` 板型、`aic_build_id`（可用
   `--expect-build-id` 固定版本）以及 tick/sched/ipc/memtest；
4. 第一条命令的响应时间超过 `--response-limit`（默认 5 s）记为 FAIL。

长稳阶段在复位周期全部 PASS 后运行 `--duration` 秒，循环执行
tick/sched/ipc/memtest；每 10 轮解析 `free` 的 `used`，相对首个采样
增长超过 `--heap-drift` 字节（默认 16384）记为 FAIL。`free` 不存在
时堆检查记 SKIP，不记 PASS。报告写入
`artifacts/d126-hil/<run_id>/report.json` 的 `stability` 字段。

### 8.2 首次刷入（BOOT-KEY 引导）

当前板上若是没有 `aicupg` 的旧固件，它无法自己进入升级模式，第一次
必须人工引导。D12p bootloader 使用 USB 升级，不支持 UART 升级；
D126 BSP 的 bootloader 配置未启用 `AICUPG_UART_ENABLE`。

1. 按住 BOOT-KEY K2，复位或重新上电，释放按键，使 BROM 进入 USB
   升级模式；
2. `artinchip-flash scan` 必须只看到一个 ArtInChip 设备；
3. 确认镜像 `Product: d126-demo124-nor` 和 SHA256 后，用 HIL 的
   bootstrap 模式完成 OS-only 刷写（同样校验 manifest SHA、Product
   和 `--partition os` 计划）：

```bash
python tools/d126_hil.py --mode full --bootstrap --skip-build \
    --port COM10 --confirm-board d126-demo124-nor --flash-scope os
```

也可以直接执行 `artinchip-flash burn <img> --partition os`。
`--bootstrap` 要求升级设备恰好一个；否则拒绝刷写。
4. 启动后确认 `msh`、`d126_info`、`aic_build_id`、`aicupg` 都存在。

首次刷入后，后续升级可由 `d126_hil.py --mode full` 自动完成：它通过
`aicupg` 写 WRI reboot reason 并 WDT 复位，再等待 USB 升级设备。
BSP 启动时会清掉 WRI 中上一次刷写留下的 software reboot reason
（例如 `REBOOT_REASON_CMD_REBOOT`），避免后续 `aicupg` 被旧值阻塞。

## 9. 异常和复位测试

1. 在串口输入非法命令，确认 FinSH 不崩溃；
2. 触发一次未安装 IRQ，确认打印 `Unhandled IRQ`；
3. 触发非法指令/访问，确认 `CPU exception:` 日志；
4. `aicupg` 后确认进入升级模式；`aicupg gotobl` 进入 bootloader
   升级模式。两者都会复位，只能在授权窗口执行。

## 10. 记录模板

| Test | Command | Result | Note |
|---|---|---|---|
| BUILD | `build-check.py d126-minimal` | PASS | 与 PACKAGE 分开记录 |
| BUILD | `build-check.py d126-utest` | PASS | 非 FPU 配置 |
| HOST | `test_d126_hil.py` | PASS | 37 个主机测试 |
| PACKAGE | `mk_firmware.py` | PASS | 真实 bootloader（SDK 输出） |
| D213 BUILD | `build-check.py d213-minimal d213-default d213-utest` | PASS | 2026-11-10，非 FPU 构建矩阵；无 D213 实机，未运行 utest/FPU |
| Plan | `d126_hil.py --mode plan` | PASS | manifest/镜像/OS 分区计划 |
| HIL M1 | `d126_hil.py --mode serial --repeat 3` | HW PASS | 2026-11-10，tick/sched/ipc/mem 各 3 次 |
| HIL M2 | `d126_hil.py --mode full` | HW PASS | 2026-11-10，含构建/打包/OS-only 刷写/utest |
| Reset x5 | `d126_hil.py --mode stability --cycles 5` | HW PASS | 2026-11-10，启动 2.81–2.85 s，msh 0.12 s |
| Long run | `d126_hil.py --mode stability --duration 120` | HW PASS | 81 轮/120.9 s，heap used 5096 不变 |
| Boot | 真实 bootloader | HW PASS | 刷写后自动重启到 msh |
| Build ID | `aic_build_id` | HW PASS | 与 manifest 一致 |
| Tick | `d126_tick 100` | HW PASS | tick=100，3/3 |
| Tick 多间隔 | `utest_run bsp.d126.tick` | HW PASS | 2026-11-10，1/2/5/10/50/100/200 ms 均 tick=expected |
| CLIC 中断屏蔽 | `utest_run bsp.d126.gpio` | HW PASS | 2026-11-10，关中断期间边沿挂起，开中断后回调；detach 后不触发 |
| Scheduler | `d126_sched 500` | HW PASS | counter=1000，3/3 |
| 优先级抢占 | `utest_run bsp.d126.scheduler` | HW PASS | 2026-11-10，高优先级线程在 sem 释放后先于低优先级线程运行 |
| IPC | `d126_ipc` | HW PASS | sem/mb PASS，3/3 |
| utest | `d126_utest.py` | HW PASS | 6 个 suite 全 PASS |
| PSRAM | `d126_memtest 1024` | HW PASS | 新固件复测 PASS |
| PSRAM/heap 边界 | `utest_run bsp.d126.memory` | HW PASS | 2026-11-10，4 MiB+3 MiB 窗口内、超 heap 请求 NULL、used 回到基线 |
| Heap 压力 | `utest_run bsp.d126.memory` | HW PASS | 8 轮 4–32 KiB 多块分配/释放，每轮 used 无漂移 |
| GPIO 输出 | `utest_run bsp.d126.gpio` | HW PASS | 2026-11-10，PD8 低/高各 3 轮回读 PASS |
| GPIO 输入 | `utest_run bsp.d126.gpio` | HW PASS | PB12 内部上拉读 1、下拉读 0 |
| GPIO 中断 | `utest_run bsp.d126.gpio` | HW PASS | PB12 输出自环：上升沿/下降沿各 1 次回调 |
| UART RX | FinSH 输入 | HW PASS | `rt_serial_device` 中断 RX（IRQ 78），msh/HIL 交互通过 |
| Display | MIPI DSI | 未实现 | |
| Touch | GT911 | 未实现 | |
