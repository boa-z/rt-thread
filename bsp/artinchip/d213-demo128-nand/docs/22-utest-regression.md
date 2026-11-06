# 22 D213 utest 回归体系

## 22.1 目标和边界

本测试固件用于把 D213 BSP 从“能启动、能手工敲命令”推进到可重复的内核与基础外设回归。

约束：

- 只修改 `bsp/artinchip/d213-demo128-nand/` 和 `bsp/artinchip/tools/`。
- 不修改 `libcpu/risc-v/common/`。纯 BSP 分支上的 `fs1` FPU 上下文失败是已知问题，不在本测试体系里通过修改通用 Port 来消除。
- 默认构建不启用 utest。`d213-minimal` 和 `d213-default` 的行为、设备注册顺序和 UART0 轮询 RX 路径保持不变。
- UART0 属于控制台。测试只验证控制台已打开和可写，不测试会破坏控制台配置的波特率切换或 RX 中断切换。
- WDT 复位、Flash 擦写、破坏性内存测试不进入自动回归。

## 22.2 构建配置

新增配置：

```text
configs/d213-utest.config
```

它在 `d213-default` 的基础上增加：

- `CONFIG_BSP_USING_UTEST=y`
- `CONFIG_BSP_UTEST_CAN_LOOPBACK` 默认关闭，只有外部 TX/RX 回环就绪时才启用
- `CONFIG_RT_USING_UTEST=y`
- `CONFIG_RT_USING_UTESTCASES=y`
- `CONFIG_RT_UTEST_SELF_PASS=y`，接入官方 `TC_uassert.c`
- `CONFIG_RT_UTEST_IRQ/SEMAPHORE/MUTEX=y`
- `CONFIG_RT_CONSOLEBUF_SIZE=256`，utest 框架要求不小于 256
- `CONFIG_FINSH_USING_OPTION_COMPLETION=y`，utest 源文件无条件引用 `RT_UTEST_MAX_OPTIONS`
- `CONFIG_RT_USING_HOOK/y`、`CONFIG_RT_HOOK_USING_FUNC_PTR=y`，官方 `core.irq` 需要中断 hook

测试构建不要使用 `--useconfig` 后直接编译。推荐使用构建矩阵脚本：

```sh
python bsp/artinchip/tools/build-check.py \
    --targets d213-utest \
    --sdk /path/to/luban-lite-jc
```

脚本会恢复原有 `.config`/`rtconfig.h`，测试固件产物在：

```text
artifacts/d213-utest/
```

也可以手工切换：

```sh
cd bsp/artinchip/d213-demo128-nand
cp configs/d213-utest.config .config
scons --pyconfig-silent
scons -j8
```

不要把 `.config` 切换后的临时结果提交为默认配置。

## 22.3 测试用例组织

```text
bsp/artinchip/d213-demo128-nand/
├── utest/
│   ├── SConscript
│   ├── context_gcc.S        # s0..s11 上下文标记和校验
│   ├── tc_smoke.c
│   ├── tc_tick.c
│   ├── tc_scheduler.c
│   ├── tc_context.c
│   ├── tc_fpu.c
│   ├── tc_uart.c
│   ├── tc_gpio.c
│   ├── tc_i2c.c
│   ├── tc_rtc.c
│   ├── tc_pwm.c
│   ├── tc_can.c
│   └── tc_display.c
├── applications/
│   ├── main.c
│   ├── selftest.c           # aic_selftest 和共享 FPU 上下文回归
│   └── fpu_test_gcc.S
└── configs/
    └── d213-utest.config
```

`utest/SConscript` 只在 `BSP_USING_UTEST=y` 时编译 `tc_*.c` 和上下文汇编。默认构建不会把测试代码链接进镜像。

### P0 内核回归

| 用例 | 覆盖内容 | 清理行为 |
|---|---|---|
| `components.utilities.utest.uassert` | 官方 utest 断言接入 | 无全局资源 |
| `bsp.d213.smoke` | utest 注册、线程句柄、堆分配 | 释放临时堆块 |
| `bsp.d213.tick` | tick 递增、`rt_thread_mdelay()`、单次软件定时器 | 删除定时器、detach 信号量 |
| `bsp.d213.scheduler` | 线程创建、退出、优先级切换、互斥量、信号量超时 | 删除工作线程、detach IPC |
| `bsp.d213.context` | 整数 callee-saved 寄存器跨切换保持 | 阻塞等待有超时 |
| `bsp.d213.fpu.context` | `fs0..fs11` 跨线程切换保持 | 双线程握手，所有等待有上限 |
| `bsp.d213.uart0` | 控制台设备已打开、TX 可写 | 不改变控制台配置 |

### P1 基础外设

| 用例 | 覆盖内容 | 硬件条件 |
|---|---|---|
| `bsp.d213.gpio` | GPIO 设备注册、`PA.0`/`PG.31` 名称解析、非法名称拒绝 | 无 |
| `bsp.d213.i2c3` | I2C3 总线访问 GT911 产品 ID | GT911 在 I2C3 上 |
| `bsp.d213.rtc` | RTC 连续读取并递增 | 32 kHz 晶振/电池 |
| `bsp.d213.pwm` | PWM0 设置、使能、关闭 | 不影响 PC.7 面板使能 |
| `bsp.d213.can0.config` | CAN0 打开、500 kbit/s、normal 模式、状态读取 | 无 |
| `bsp.d213.can0.loopback` | CAN0 SELFTEST 发送并接收一帧 | 需要外部 TX/RX 回环，或收发器+第二节点；默认不编译 |
| `bsp.d213.spinand.ro` | JEDEC ID/几何、`os` 页 0 重复读 100 次、越界拒绝、坏块标记、写/擦/移动/标坏必须返回 `-RT_EPERM` | D213 板载 SPI NAND；无 NAND 时打印跳过信息，不误报通过 |

`bsp.d213.spinand.ro` 需要 `BSP_USING_SPINAND_MTD_NAND`；当前
`configs/d213-utest.config` 已开启。该用例全程只读，不会产生
Program/Erase/Refresh 命令。

CAN 外部总线收发仍需要：

- 收发器；
- 两端 120 Ω 终端；
- 第二个 CAN 节点或分析仪；
- 双方相同波特率。

缺少这些条件时，外部总线测试只能记 `SKIP`，不能把控制器 loopback 当作外部总线通过。

2026-11-05 D213 实机验证结果：在没有外部 TX/RX 回环、也没有第二节点时，
`bsp.d213.can0.loopback` 得到 `write=0`、`snd=0 drop=1`、`ackerr=0`，
RX 等待超时。原厂 bare-metal 示例也注明 SELFTEST 需要连接 CAN TX/RX。
因此默认 `d213-utest` 只保留 `bsp.d213.can0.config`；只有准备外部回环或
对端后，才打开 `BSP_UTEST_CAN_LOOPBACK` 并运行 loopback 用例。

### P2 显示和触摸

`bsp.d213.display.vsync` 只验证显示设备、1024x600x32 参数和 VSync 计数递增。以下项目保留为人工/仪器测试：

- 彩条、花屏、闪烁、偏移；
- 双缓冲 pan 和长时间 underflow；
- GE FillRect/BitBlt/Scale/Blend/Rotate；
- GT911 四角坐标校准和多点轨迹。

已有的 `disp_info`、`disp_test`、`ge_test`、`touch_test` MSH 命令保留，用于人工验证。

## 22.4 实机运行

烧录 `artifacts/d213-utest/images/` 下生成的测试镜像，打开 UART0，串口参数 115200 8N1。

```text
msh > utest_list
msh > utest_run -thread components.utilities.utest.uassert
msh > utest_run -thread core.irq
msh > utest_run -thread core.semaphore
msh > utest_run -thread core.mutex
msh > utest_run -thread core.mutex_pi
msh > utest_run -thread bsp.d213.*
```

注意：

- 使用 `-thread` 让测试在独立的 8192 字节 utest 线程中运行，不占用 tshell 的 4096 字节栈；
- 不要用 `utest_run` 不带 `-thread` 跑整套测试；
- BSP 自定义用例的线程/IPC 等待都有上限；主机侧对每个 suite 另有超时；
- `bsp.d213.fpu.context` 在纯 BSP 上预期失败，详见 22.6。

官方 `core.timer` 内含 `3 x 60 s` 的 100 定时器压力测试，默认测试配置不启用；需要长时间压力时再单独打开 `RT_UTEST_TIMER` 并放宽主机超时。

## 22.5 主机侧串口脚本

脚本：

```text
bsp/artinchip/tools/d213_utest.py
```

依赖 pyserial：

```sh
python -m pip install pyserial
```

运行：

```sh
python bsp/artinchip/tools/d213_utest.py \
    --port COM7 \
    --log artifacts/d213-utest-serial.log
```

可选参数：

```text
--baud 115200
--timeout 180                 每个 suite 的超时秒数
--suite core.irq              追加/覆盖 suite，可重复
--known-fail <name>           追加已知失败用例
--no-list                     跳过 utest_list
```

脚本会：

- 等待 `msh >`；
- 发送 `utest_list`，检查预期 suite 是否真的进入固件；
- 逐个发送 `utest_run -thread <suite>`；
- 解析 `PASSED` / `FAILED` / `NOT FOUND`；
- 检测启动横幅，判断是否发生复位；
- 检测超时；
- 保存完整串口日志和每个 suite 的耗时。

退出码：

```text
0  PASS
1  FAIL / TIMEOUT / RESET
2  仅存在已知失败（KNOWN-FAIL）
```

脚本不会复位、烧录或擦除目标板。

## 22.6 FPU 已知失败

纯 BSP 使用上游 `libcpu/risc-v/common` 上下文切换实现。该实现不保存 `fs0..fs11` 和 `fcsr`，因此：

```text
fpu context: fs1 was not preserved across the switch
fpu context: fs1 is the known pure-BSP common-port limitation
[  FAILED  ] [ result   ] testcase (bsp.d213.fpu.context)
```

这是预期结果，不是 D213 板卡故障，也不通过修改 `libcpu` 来让测试变绿。

验证 FPU 修复必须使用独立集成分支/独立通用 Port 修复：

```text
integration/artinchip-fpu-test
```

在该分支上同一个 `bsp.d213.fpu.context` 应报告 PASS。只有在 FPU 修复真正进入上游 Common Port 后，才能把本用例从 `KNOWN-FAIL` 改为普通 PASS。

## 22.7 结果判定

每个结果必须带证据级别：

| 标记 | 含义 |
|---|---|
| BUILD PASS | 只证明编译、链接成功 |
| STATIC PASS | ELF/map/符号/字符串检查通过 |
| PACKAGE PASS | 只证明 `.itb`/`.img` 打包成功 |
| HW PASS | 实机命令完成且输出符合预期 |
| HW FAIL | 实机执行失败 |
| KNOWN-FAIL | 已知且已记录的失败，不能算 HW PASS |
| NOT-TESTED | 未执行或缺少外部条件 |
| SKIP | 条件不足，明确不判通过或失败 |

推荐记录模板：

```text
测试项：
镜像 SHA256：
配置：d213-utest
命令：
预期：
实际：
证据级别：BUILD PASS / STATIC PASS / PACKAGE PASS / HW PASS / HW FAIL / KNOWN-FAIL / NOT-TESTED / SKIP
备注：
```

当前仓库内能提供的证据：

- `d213-minimal`、`d213-default`、`d213-utest` 的 BUILD PASS；
- `d213-utest` ELF 的 STATIC PASS，包括 utest 符号和用例名字符串；
- 测试固件的 PACKAGE PASS（需要本地 Luban-Lite SDK）；
- 之前的 UART0/tick/display/GE/GT911/I2C3/WDT/RTC 手工实机结果；
- 本轮 utest 套件的实机结果：`NOT-TESTED`（当前环境没有连接 D213 板卡）。

## 22.8 后续扩展

建议按以下顺序补齐：

1. 在实机上跑通 P0，并记录每个 suite 的日志和耗时；
2. 用第二节点/分析仪完成 CAN 外部总线测试；
3. 增加 GPIO 外部短接环回用例，默认关闭，避免误驱动板级功能引脚；
4. 增加 GE FillRect/BitBlt 的 utest 版本；
5. 增加显示长时间刷新和 underflow 统计；
6. 将主机脚本接入 CI 的硬件 runner，但 BUILD/STATIC 结果不能替代 HW 结果。
