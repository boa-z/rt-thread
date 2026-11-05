# 后续支持和上游化建议

## 1. 当前分支策略

```text
master                         上游基线，不直接修改
bsp/artinchip-d21x             当前 ArtInChip 集成基线
feat/artinchip-d126-bringup    D126 开发分支
contrib/artinchip-d126-*       后续独立上游候选补丁
```

原则：

- 不在 `master` 上开发；
- 不重写公开分支历史；
- 不把 `upstream/d21x-candidate` 当日常开发分支；
- 不自动合并或强推远程；
- 每个阶段单独提交；
- 不破坏 D213 已验证功能。

## 2. 当前结构

```text
bsp/artinchip/
├── d213-demo128-nand/          # D21x/C906 RV64
└── d126-demo124-nor/           # D12p/E907F RV32
    ├── cpu/                    # BSP-local E9xx CPU port
    ├── board/
    ├── drivers/
    ├── pack/
    └── tools/
```

D126 的 CPU 端口暂时放在 BSP 内，原因是：

- 还没有第二块 D12p/D13x 板验证；
- CLIC/TSPEND/FPU 细节需要实机确认；
- 避免为了复用而修改 `libcpu/` 影响其他 RISC-V BSP。

## 3. 何时提升到公共层

满足以下条件后，才建议把 `cpu/` 提升到
`libcpu/risc-v/artinchip/e9xx/`：

1. D126 实机启动、tick、FPU、异常测试通过；
2. D133 或第二块 D12p 板复用同一 CPU 端口并通过启动；
3. 确认 CLIC `mtvec/mtvt`、TSPEND、CORET、FPU 保存恢复一致；
4. D213 回归构建和实机功能不退化；
5. `libcpu/risc-v/SConscript` 增加 `VENDOR/CPU` 分派，且不影响其他 BSP。

提升时保持 BSP 的 `SConstruct` 使用 `has_libcpu=False` 或显式选择
新端口，避免和 `common` 的 `trap_entry`/`SW_handler` 重复。

## 4. 建议的长期目录

```text
bsp/artinchip/
├── libraries/
│   ├── hal/                    # IP 级 HAL（验证后）
│   ├── drivers/                # RT-Thread 设备适配（验证后）
│   ├── soc/
│   │   ├── d21x/               # CMU/GPIO/IRQ 表
│   │   └── d12p/               # CMU/GPIO/IRQ 表
│   └── include/
├── d213-demo128-nand/
└── d126-demo124-nor/
```

注意：

- 该结构不是一次性重构目标；
- 只有实际复用需求出现时才移动文件；
- SoC 条件（时钟、复位、IRQ、group 映射、DMA 属性）必须留在
  `soc/`，不能塞进公共 IP 层；
- 公共层不能依赖 Luban-Lite 的 `aic_core.h`/`aicos_*` 抽象。

## 5. 上游补丁拆分建议

建议按以下顺序整理 `contrib/artinchip-d126-*`：

1. `[bsp][artinchip] add D126 board documentation and Kconfig`
2. `[bsp][artinchip] add D12p CLIC/CORET startup`
3. `[bsp][artinchip] add D126 UART console and tick`
4. `[bsp][artinchip] add D126 GPIO pin driver`
5. `[bsp][artinchip] add D126 packaging scripts`
6. `[libcpu][riscv] add ArtInChip E9xx CPU port`（仅在验证后）
7. `[bsp][artinchip] add D126 display/touch`（仅在硬件确认后）

不要提交：

- 原厂 `bootloader.bin`；
- `d12p.pbp`；
- 任何 eFuse/安全启动密钥；
- 生成的 `images/`、`.elf`、`.bin`、`.map`。

## 6. D133 和其他 D12p 板

D13x 也使用 E907F 系列和 CLIC/CORET，可优先复用：

- `cpu/` 的 CLIC/上下文切换；
- UART 16550 寄存器定义；
- GPIO 通用寄存器访问；
- I2C v1.0 HAL（如果 D13x 也是 v1.0）。

必须按板/SoC 独立确认：

- PSRAM/NOR 容量和启动布局；
- CMU 版本（D13x 与 D12p 可能不同）；
- GPIO group 和 pinmux；
- 显示接口（RGB/LVDS/MIPI）；
- CAN/CAN FD；
- bootloader/PBP 资源。

建议新增板卡时：

1. 复制 `d126-demo124-nor` 作为模板；
2. 只改 `board/`、`pack/`、`.config`；
3. 先保持 `cpu/` 不动，验证启动；
4. 确认 CPU 端口一致后再提取公共层；
5. 每块板保留独立的 `docs/01-board-mapping.md` 和测试记录。

## 7. 上游验收门槛

- D213 默认构建 PASS；
- D213 + LVGL 外部组件构建 PASS；
- D126 最小构建 PASS；
- D126 ELF/ITB/IMG 打包 PASS；
- D126 实机启动日志、tick、FPU、PSRAM、GPIO 记录；
- D126 显示/触摸记录（如果包含该补丁）；
- 文档明确区分 BUILD/PACKAGE/HW/PENDING；
- 不包含原厂二进制和生成物。
