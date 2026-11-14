# 构建、检查和打包

## 1. 工具链

已验证：

```text
riscv64-unknown-elf-gcc 16.1.0
Newlib 4.6.0
```

目标参数：

```text
-march=rv32imafc_zifencei
-mabi=ilp32f
-mcmodel=medlow
```

GCC 11 及更早版本不接受 `_zifencei`，`rtconfig.py` 会根据编译器版本
自动选择。当前 Homebrew GCC 16 需要 `_zifencei`。

编译器必须生成 ELF32、RISC-V、RVC、single-float ABI。`check_elf.py`
会检查这些字段。

## 2. 构建

```bash
cd bsp/artinchip/d126fxr-demo-v1-1

# 只有修改 .config 后才需要
RTT_EXEC_PATH=/opt/homebrew/bin \
  /Users/boa/Documents/dev/rt-thread/.venv/bin/scons --pyconfig-silent

RTT_EXEC_PATH=/opt/homebrew/bin \
  /Users/boa/Documents/dev/rt-thread/.venv/bin/scons -j8

RTT_EXEC_PATH=/opt/homebrew/bin \
  python3 tools/check_elf.py --elf rtthread.elf
```

当前结果（字段值随配置变化）：

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

`rtthread.bin` 由 ELF 的 PT_LOAD 段生成，大小等于 `seg0.bin`。
BSS 不进入镜像，由 common `riscv_bss_init()` 清零。

## 3. ELF 检查内容

共享检查器 `bsp/artinchip/tools/check_elf.py` 检查：

- ELF32/ELF64、RISC-V、RVC、单/双精度 ABI；
- entry 和 `_start` 都是 `0x40000000`；
- `system_vectors` 64 字节对齐；
- `__text_start == entry`；
- BSS、`.percpu`、heap 不重叠；
- 必需 symbol：`system_vectors`、`rt_hw_trap`、
  `rt_hw_interrupt_init`、`rt_hw_context_switch*`、
  `riscv_timer_rdtime`、`riscv_timer_set_compare`、
  `platform_get_plic_base` 或 `platform_get_clic_base`；
- 拒绝旧的 `__Vectors`、`Default_IRQHandler`、`PendSV_Handler`、
  `trap_entry`、`SW_handler`；
- 可选 `--map` 检查旧 `libcpu/rv64`、`common64`、`legacy_common`
  或 BSP `cpu/` 对象没有参与链接。

它不能证明镜像可以启动；HW 验证仍需串口日志。

## 4. 打包流程

`tools/mk_firmware.py` 调用 Luban-Lite `tools/scripts` 中的纯 Python
工具：

```text
rtthread.elf
  -> elf2image -> d12p_os.its + seg0.bin
  -> itbimage -> d12p_os.itb

pbp_cfg.json + partition.json
  -> mk_private_resource.py -> raw private data
  -> mk_prebootprog.py -m PDAT -> pbp_cfg.bin

bootloader.bin + pbp_cfg.bin + d12p.pbp
  -> aicimage -> bootloader.aic / usbupg-psram-init.aic

env.txt
  -> mkenvimage -> env.bin

所有组件 + image_cfg.json
  -> mk_image.py -> d12p_d126-demo124-nor_v1.0.0.img
```

使用占位 bootloader 时，最终文件会改名为
`d12p_d126-demo124-nor_v1.0.0_PLACEHOLDER_DO_NOT_FLASH.img`。

不提交原厂 `bootloader.bin`、`d12p.pbp` 或其他再分发权限不明确的
二进制。打包脚本只从用户提供的 SDK/路径读取。

### 真实固件

```bash
# 自动使用 SDK 中已构建的 d126-demo124-nor bootloader
AIC_SDK_DIR=/path/to/luban-lite-jc \
python3 tools/mk_firmware.py \
  --elf rtthread.elf \
  --psram-size 8m \
  --out images

# 或显式指定
python3 tools/mk_firmware.py \
  --elf rtthread.elf \
  --bootloader /path/to/d126-demo124-nor/bootloader.bin \
  --psram-size 8m \
  --out images
```

自动查找顺序：

1. `--bootloader`；
2. `<sdk>/target/d12p/d126-demo124-nor/pack/bootloader.bin`；
3. `<sdk>/output/d12p_d126-demo124-nor_*/images/bootloader.bin`（多个
   构建时取最新）。

`bootloader.bin` 必须是为同一 target、同一 PSRAM 容量和同一 SPI NOR
配置构建的。不要使用 demo88/demo132 或其他板卡的 bootloader。找不到
真实 bootloader 时打包直接失败；占位镜像只能通过显式
`--allow-placeholder-bootloader` 生成。

原厂 bootloader 的参考 defconfig：

```text
target/configs/d12p_d126-demo124-nor_baremetal_bootloader_defconfig
```

原厂工具链为 Xuantie-900 GCC（V2.6.1 或 V3.2.0）Linux x86_64 版本。
当前 macOS 主机没有可用的原厂 Linux 工具链，因此本仓库只在打包
阶段消费已经构建好的 `bootloader.bin`，不在 macOS 上构建原厂
bootloader。

### CI/流程验证

```bash
python3 tools/mk_firmware.py \
  --sdk /path/to/luban-lite-jc \
  --elf rtthread.elf \
  --out images \
  --allow-placeholder-bootloader
```

输出镜像包含占位 bootloader，**不可启动、不可烧录**，只用于验证
打包工具链和分区布局。

## 5. 分区和镜像

```text
16 MB SPI NOR
  0x000000  spl       512 KiB
  0x080000  env       128 KiB
  0x0a0000  env_r     128 KiB
  0x0c0000  userid    256 KiB
  0x100000  os        4 MiB
  0x500000  rodata    9 MiB
  0xe00000  data      1 MiB
```

当前最小 BSP 只打包 `spl`、`env`、`env_r`、`os`。`rodata`/`data`
分区保留但为空。

## 6. 手工烧录检查清单

本 BSP 不自动烧录。烧录前确认：

1. 串口 UART0 已连接，115200 8N1；
2. 使用的是真实 D126 bootloader，不是占位文件；
3. PSRAM 容量与 bootloader/PBP 一致；
4. 镜像版本和分区表匹配 16 MB SPI NOR；
5. 不覆盖 `userid`、eFuse、安全启动状态；
6. 保留原厂 bootloader/PBP 备份；
7. 首次烧录只写 `spl` 和 `os`，确认能进 BROM/升级模式。

具体烧录工具名称、版本和命令需要在 D126 实物上确认后补充。

## 7. 本次验证记录

日期：2026-11-06（占位 bootloader，仅验证打包流程）

```text
编译器      riscv64-unknown-elf-gcc 16.1.0
Newlib      4.6.0
配置        默认 .config（GPIO + console，无显示/LVGL）
ELF         47608 text / 744 data / 10064 bss
rtthread.bin 48360 bytes
d12p_os.itb  51200 bytes
最终 .img    134656 bytes（占位 bootloader，文件名带 PLACEHOLDER）
```

日期：2026-11-09（真实 bootloader，可由 `build-check.py` 复现）

```text
编译器      riscv64-unknown-elf-gcc 10.2.0
配置        d126-minimal
ELF         46824 text / 1528 data / 9744 bss
rtthread.bin 48360 bytes
d12p_os.itb  51200 bytes
最终 .img    593408 bytes
OS-only .img 329728 bytes（只含 image.target.os）
bootloader   233360 bytes（SDK target/d12p/d126-demo124-nor/pack）
```

同一天的首版固件在实机上停在上电 banner 之后。定位到两个 BSP 问题：
原厂 SPL 遗留的 E907 `MEXSTATUS.SPUSHEN/SPSWAPEN` 与 common port 的
软件 IRQ 栈切换冲突（详见 `docs/02-architecture.md` §3），以及
`board/timer.c` 按原厂头文件注释把 CORET MTIME 低字取在 `+0x7ffc`
（实际低字在 `+0x7ff8`），导致首个 tick 后 MTIP 无法撤销。

实机复测（2026-11-09，OS-only 镜像）：

```text
启动       到达 msh，MEXSTATUS boot 0x00030010 -> now 0x00000010
tick       d126_selftest: tick=100
memtest    d126_memtest 1024: PASS
FPU 线程   worker 1/2 FAIL（common port 线程切换未保存 f0-f31/fcsr，
           已知未合并项，不在 BSP 内修复）
```

RT-Thread 版本字符串包含构建日期/时间，因此每次重新构建的 ELF/ITB/IMG
哈希都会变化；不要在上游补丁或发布说明中写死哈希。需要记录时在
**同一次构建产物**上执行：

```bash
shasum -a 256 rtthread.bin images/d12p_os.itb \
  images/d12p_d126-demo124-nor_v1.0.0*.img
```

注意：2026-11-06 的记录使用占位 bootloader，仅用于验证打包流程，不能
启动。2026-11-09 的记录使用 SDK 中为 d126-demo124-nor 构建的真实
bootloader。实机曾进入应用 banner（UART0 输出正常）后在首个中断处
异常；根因已在 BSP 层修复，实机已达 msh，tick 和 memtest 通过。
FPU 线程上下文仍依赖尚未合并的 common port 修复。
