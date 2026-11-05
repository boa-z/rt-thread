# 测试与实机验证清单

## 1. 测试分级

| 级别 | 含义 | 当前状态 |
|---|---|---|
| BUILD | 交叉编译、链接、ELF 检查 | PASS |
| HOST | 打包脚本、静态检查 | PASS |
| PACKAGE | ITB/IMG 生成（占位 bootloader） | PASS |
| HW | D126 实物启动和外设 | PENDING |

只有 BUILD/HOST/PACKAGE 通过，不能说明固件可以启动。

## 2. 构建检查

```bash
cd bsp/artinchip/d126-demo124-nor
RTT_EXEC_PATH=/opt/homebrew/bin \
  /Users/boa/Documents/dev/rt-thread/.venv/bin/scons -j8
RTT_EXEC_PATH=/opt/homebrew/bin \
  python3 tools/check_elf.py --elf rtthread.elf
```

预期：

```text
ArtInChip RISC-V check: PASS
  ELF          : ELF32, single-float ABI
  entry/_start : 0x40000000
  trap vector  : 0x40000180 (direct mode)
  controler    : CLIC
  BSS          : ...
  percpu       : ...
  heap         : ... -> 0x40800000
```

地址会随代码/配置变化，但 entry 必须为 `0x40000000`，
`system_vectors` 必须 64 字节对齐，且不应出现旧的 `__Vectors`/
`PendSV_Handler`。

## 3. 打包检查

```bash
AIC_SDK_DIR=/path/to/luban-lite-jc \
python3 tools/mk_firmware.py \
  --elf rtthread.elf \
  --out images \
  --allow-placeholder-bootloader
```

预期生成：

```text
images/d12p_os.itb
images/d12p_d126-demo124-nor_v1.0.0_PLACEHOLDER_DO_NOT_FLASH.img
```

检查 ITS：

```bash
cat images/d12p_os.its
```

必须包含：

```text
load = <0x40000000>;
entry = <0x40000000>;
```

**占位 bootloader 镜像不可启动，不能烧录。**

## 4. 实机启动测试

连接 UART0：

```text
115200 8N1，PA0=TX，PA1=RX
```

记录完整日志，至少包含：

1. BROM/PBP 版本和 PSRAM 初始化日志；
2. SPL/bootloader 加载 `d12p_os.itb` 的日志；
3. RT-Thread 版本和 BSP banner；
4. `main: running`；
5. `selftest: tick=..., fpu=OK`；
6. `msh >` 提示符。

RT-Thread banner 示例：

```text
ArtInChip D126 (D12p) RT-Thread BSP
CPU: T-Head E907F (RV32IMafc), 480000000 Hz
UART0: 24000000 Hz, 115200 baud
PSRAM: 0x40000000, 8 MB
console: polled UART0
...
msh >
```

如果 UART 输出乱码：

- 先确认 PLL_INT1 和 UART0 分频；
- 用示波器测 PA0 波特率；
- 检查 `clock.c` 的 PLL 读取公式；
- 检查 PA0/PA1 pinmux 是否为 `0x325`。

如果完全没有输出：

- 检查 bootloader 是否真的跳转到 `0x40000000`；
- 检查 `rtthread.bin` 是否被正确加载；
- 用 JTAG 读取 `mepc/mtval/mcause`；
- 检查 PSRAM 初始化和容量。

## 5. Tick / FPU 测试

```text
msh > d126_selftest
fpu worker 1: OK
fpu worker 2: OK
selftest: tick=100, fpu=OK
```

判定：

- `tick` 约等于 100（100 ms）；
- 两个 FPU 线程都 OK；
- 系统不重启、不卡死。

如果 `tick` 明显偏离 100，CPU 频率或 CORET 配置不正确。

## 6. PSRAM / heap 测试

```text
msh > d126_memtest 1024
memtest: 1024 KiB @ 0x...: PASS
```

建议再测：

```text
msh > d126_memtest 4096
```

边界：

- 默认 heap 约 8 MB；
- 如果实际是 16 MB PSRAM，当前 BSP 只使用前 8 MB；
- 不要用 `d126_memtest` 测试 heap 之外的地址，可能踩到未初始化 PSRAM。

## 7. GPIO 测试

使用 RT-Thread pin 框架：

```text
msh > pin num PA.0
msh > pin mode PA.0 output
msh > pin write PA.0 high
msh > pin read PA.0
msh > pin write PA.0 low
```

注意：PA0/PA1 是 UART0，不能拿来做 GPIO 测试。建议选未使用的
PA2/PA3 或其他已确认安全的引脚。当前没有确认 LED/按键引脚，
因此 GPIO 实机测试为 PENDING。

GPIO 中断测试需要先确认引脚和外部上拉/下拉，未提供默认测试。

## 8. 异常和复位测试

可选：

1. 在串口输入非法命令，确认 FinSH 不崩溃；
2. 触发一次未安装 IRQ，确认打印 `Unhandled IRQ`；
3. 触发非法指令/访问，确认 `CPU exception:` 日志；
4. 软复位，确认重新进入 bootloader 和 RT-Thread。

## 9. 记录模板

| 测试 | 命令/条件 | 结果 | 备注 |
|---|---|---|---|
| BUILD | `scons -j8` | PASS | GCC 16.1.0 |
| ELF | `check_elf.py` | PASS | entry 0x40000000 |
| PACKAGE | `mk_firmware.py` | PASS | 占位 bootloader |
| Boot | 真实 bootloader | PENDING | |
| Tick | `d126_selftest` | PENDING | |
| FPU | `d126_selftest` | PENDING | |
| PSRAM | `d126_memtest 1024` | PENDING | |
| GPIO | `pin` 命令 | PENDING | 引脚待确认 |
| UART RX | FinSH 输入 | PENDING | |
| Display | MIPI DSI | 未实现 | |
| Touch | GT911 | 未实现 | |
