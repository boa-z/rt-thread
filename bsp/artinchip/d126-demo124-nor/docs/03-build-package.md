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
cd bsp/artinchip/d126-demo124-nor

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

pbp_cfg.json
  -> aicprivate -> pbp_cfg.bin

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
AIC_SDK_DIR=/path/to/luban-lite-jc \
python3 tools/mk_firmware.py \
  --elf rtthread.elf \
  --bootloader /path/to/d126-demo124-nor/bootloader.bin \
  --psram-size 8m \
  --out images
```

`bootloader.bin` 必须是为同一 target、同一 PSRAM 容量和同一 SPI NOR
配置构建的。不要使用 demo88/demo132 或其他板卡的 bootloader。

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

日期：2026-11-06

```text
编译器      riscv64-unknown-elf-gcc 16.1.0
Newlib      4.6.0
配置        默认 .config（GPIO + console，无显示/LVGL）
ELF         47528 text / 744 data / 6992 bss
rtthread.bin 48280 bytes
d12p_os.itb  51200 bytes
最终 .img    134656 bytes（占位 bootloader，文件名带 PLACEHOLDER）
```

RT-Thread 版本字符串包含构建日期/时间，因此每次重新构建的 ELF/ITB/IMG
哈希都会变化；不要在上游补丁或发布说明中写死哈希。需要记录时在
**同一次构建产物**上执行：

```bash
shasum -a 256 rtthread.bin images/d12p_os.itb \
  images/d12p_d126-demo124-nor_v1.0.0*.img
```

注意：最终 `.img` 使用占位 bootloader，仅用于验证打包流程，不能启动。
真实固件必须在替换为对应 D126 bootloader 后重新生成并记录。
