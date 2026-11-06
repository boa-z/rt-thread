# ArtInChip RT-Thread BSPs

本目录包含两个基于 RT-Thread **Next RISC-V common port**（PR #10320）
的 ArtInChip BSP：

```text
bsp/artinchip/
├── d213-demo128-nand/   D213EC-DEMO-V4-0, C906 RV64GC, PLIC + CLINT
├── d126-demo124-nor/    D126FxR-DEMO-V1-1, E907F RV32, CLIC + CORET
└── tools/
    ├── check_elf.py     shared ELF/map checker
    └── build-check.py   D213 minimal/default + D126 minimal matrix
```

## 基线

```text
RT-Thread upstream master: 89c70abccd0359f9f7ba9df2e81dbe764d3e78d3
New common port:           PR #10320 "[ARCH/RISC-V] Next RISC-V"
```

两个 BSP 都设置：

```text
CONFIG_RT_USING_RISCV_NEW_COMMON=y
CONFIG_ARCH_RISCV_M_MODE=y
CONFIG_RT_USING_ADT=y
CONFIG_RT_USING_ADT_BITMAP=y
```

D213 use `ARCH_RISCV64` + `ARCH_RISCV_FPU_D`; D126 uses `ARCH_RISCV32` +
`ARCH_RISCV_FPU_S`.

## 快速构建

```bash
export RTT_EXEC_PATH=/opt/homebrew/bin

# 一键构建、静态检查、打包三个配置
python3 bsp/artinchip/tools/build-check.py \
  --sdk /path/to/luban-lite-jc
```

或单独构建：

```bash
cd bsp/artinchip/d213-demo128-nand
scons -j8
python3 tools/check_elf.py

cd ../d126-demo124-nor
scons -j8
python3 tools/check_elf.py
```

`build-check.py` 会：

1. 备份当前 `.config`/`rtconfig.h`；
2. 应用 `configs/*.config`；
3. 重新生成 `rtconfig.h`；
4. `scons -c` + `scons -j8`；
5. 运行共享 ELF/map 检查；
6. 打包（D213 真实 SDK 资源；D126 占位保护）；
7. 把 ELF/BIN/IMG 和日志放到 `artifacts/`；
8. 生成 `artifacts/manifest.md`；
9. 恢复原 `.config`/`rtconfig.h`。

`artifacts/` 被 `.gitignore` 排除，不进入源码历史。

## 支持矩阵

| 配置 | 说明 | 构建 | 打包 | 实机 |
|---|---|---|---|---|
| `d213-minimal` | UART0、tick、FinSH、FPU 自检 | PASS | PASS | PENDING |
| `d213-default` | 原默认外设 + LVDS/DE + GE + GT911 | PASS | PASS | PENDING |
| `d126-minimal` | UART0、tick、GPIO、CLIC/CORET | PASS | PASS（占位） | PENDING |

D126 真实固件需要 `d126-demo124-nor` 的原厂 `bootloader.bin`。没有
真实 bootloader 时，`mk_firmware.py` 生成的镜像会带
`PLACEHOLDER_DO_NOT_FLASH`，不可烧录。

## 重要限制

- BUILD/STATIC/PACKAGE PASS 不等于 HW PASS。
- D213 实机验证需要 D213EC-DEMO-V4-0、UART0、原厂升级工具。
- D126 实机验证需要 D126FxR-DEMO-V1-1、真实 bootloader、确认 PSRAM
  容量/UART 引脚。
- 不允许自动烧录、擦 eFuse 或修改安全启动状态。
- 原厂 Bootloader/PBP/IMG 不提交到 Git。

实机步骤见 `TEST-TOMORROW.md`。
