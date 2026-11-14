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
├── tools/                      # shared ELF/build checks
├── d213ecv-demo-v4-0/          # D21x/C906 RV64
│   ├── board/                  # PLIC/C906 hooks
│   └── configs/
└── d126fxr-demo-v1-1/          # D12p/E907F RV32
    ├── board/                  # CLIC/CORET hooks
    ├── configs/
    ├── drivers/
    ├── pack/
    └── tools/
```

两个 BSP 都设置 `RT_USING_RISCV_NEW_COMMON=y`，CPU 实现来自：

```text
libcpu/risc-v/common/
```

D126 原来 BSP 内的 `cpu/` 已删除，不能继续维护第二套线程切换/Trap。

## 3. 何时需要公共层

当前不需要把 CLIC/CORET 提取到 `bsp/artinchip/common`。满足以下条件
后再评估：

1. D126 实机启动、tick、FPU、异常测试通过；
2. D133 或第二块 D12p 板复用同一套 platform hook；
3. 确认 CLIC direct/SHV 模式、CORET 偏移和 FPU 保存恢复一致；
4. D213 回归构建和实机功能不退化；
5. 公共层不引入 ArtInChip 特有逻辑到 `libcpu/`。

需要修改 `libcpu/` 时，单独提交并说明为什么 BSP hook 不足。本分支
已经为 common context switch 增加 FPU/fcsr 保存，这是两个 BSP 都
需要的架构修复。

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
├── d213ecv-demo-v4-0/
└── d126fxr-demo-v1-1/
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

- `libcpu/risc-v/common` 的 CLIC/上下文切换/Timer 核心；
- 本 BSP 的 CLIC/CORET platform hook；
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

1. 复制 `d126fxr-demo-v1-1` 作为模板；
2. 只改 `board/`、`pack/`、`.config`；
3. 继续使用 `RT_USING_RISCV_NEW_COMMON`，不要新增 BSP `cpu/`；
4. 确认 CLIC/CORET hook 一致后再提取公共 SoC 层；
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
