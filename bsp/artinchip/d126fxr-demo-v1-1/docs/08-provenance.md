# 代码来源和许可说明

本 BSP 只包含 Apache-2.0 兼容的源码和配置，不包含原厂闭源
Bootloader、PBP 二进制或再分发权限不明确的资源。

## 1. 本仓库新增代码

- `board/interrupt.c`（CLIC platform hooks）
- `board/timer.c`（CORET accessors）
- `board/clock.c`
- `board/uart.c`
- `board/board.c`
- `drivers/drv_gpio.c`
- `applications/main.c`
- `tools/check_elf.py`
- `tools/mk_firmware.py`

以上代码由本移植工作编写，许可为 Apache-2.0。CPU 启动、Trap、上下文
切换、IRQ 栈和 Timer 核心来自 RT-Thread upstream
`libcpu/risc-v/common`；本分支只对 common 的 FPU/fcsr 上下文保存做了
最小修复。

## 2. 参考的 ArtInChip Luban-Lite 源码

以下文件用于理解硬件行为，未整体复制：

```text
bsp/artinchip/sys/d12p/startup_gcc.S
bsp/artinchip/sys/d12p/vectors.S
bsp/artinchip/sys/d12p/system.c
bsp/artinchip/sys/d12p/time.c
bsp/artinchip/sys/d12p/isr.c
bsp/artinchip/sys/d12p/aic_drv_irq.c
bsp/artinchip/sys/d12p/link_script/gcc_aic.ld.S
bsp/artinchip/sys/d12p/ram_param.c
bsp/artinchip/hal/gpio/aic_hal_gpio.c
bsp/artinchip/hal/i2c/hal_i2c.c
target/d12p/d126-demo124-nor/
```

Luban-Lite 这些文件带有 Apache-2.0 声明。

## 3. 复制到本 BSP 的配置

```text
pack/image_cfg.json   派生自 target/d12p/d126-demo124-nor/pack/image_cfg.json
pack/pbp_cfg.json     复制自 target/d12p/d126-demo124-nor/pack/pbp_cfg.json
pack/partition.json   按 Luban-Lite 构建生成的 partition.json 复制（分区表）
pack/env.txt          复制自 target/d12p/d126-demo124-nor/pack/env.txt
```

这些是板级配置数据，不是 Bootloader/PBP 二进制。保留原厂注释和
来源说明。若后续 SDK 更新，应同步更新并重新验证。

## 4. 未提交的二进制

以下文件不进入 Git：

```text
bootloader.bin
bootloader.aic
d12p.pbp
usbupg-psram-init.aic
*.img
*.itb
```

打包脚本从用户提供的 Luban-Lite SDK 或显式路径读取它们。CI 使用
占位 bootloader 时，生成物明确标记为不可启动。

## 5. RT-Thread 代码

本 BSP 复用 RT-Thread 上游的：

- 内核、FinSH、设备框架、pin 框架；
- 内存管理、IPC、libc 适配；
- 构建系统；
- `libcpu/risc-v/common` 启动、Trap、上下文切换、IRQ 栈、Timer。

本分支对 `libcpu/risc-v/common/context_gcc.S` 和 `stack.c` 做了
FPU/fcsr 上下文保存补丁，原因和影响记录在对应提交中。未修改
RT-Thread 内核 `src/`、`components/` 或构建系统。

## 6. 上游提交注意

向 RT-Thread 上游提交时：

- 不提交原厂 Bootloader/PBP；
- 不提交生成的 `.img/.itb/.bin`；
- 在 commit message 中说明硬件验证状态；
- 明确区分 BUILD、PACKAGE、HW、PENDING；
- 如果复制了 Luban-Lite 代码，保留 Apache-2.0 头和来源说明。
