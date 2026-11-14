# ArtInChip BSP 说明

本目录提供 ArtInChip D21x / D12p 系列开发板的 RT-Thread BSP。两个 BSP 均基于
RT-Thread Next RISC-V common port（`libcpu/risc-v/common`，PR #10320），运行在
RISC-V M-mode。

## 开发板

| 开发板 | SoC | CPU | BSP 目录 | 官方资料 |
|---|---|---|---|---|
| ArtInChip D213ECV-DEMO-V4-0 | D213ECV（D21x） | T-Head C906，RV64GC | [`d213ecv-demo-v4-0`](d213ecv-demo-v4-0/README.md) | [产品页面](https://aicdoc.artinchip.com/topics/product/d213ecV4.html) |
| ArtInChip D126FxR-DEMO-V1-1 | D126（D12p） | T-Head E907F，RV32IMafc | [`d126fxr-demo-v1-1`](d126fxr-demo-v1-1/README.md) | [快速上手](https://aicdoc.artinchip.com/topics/quickstart/d126-boards-d12xpro.html#d126fxv-demo-v2-0) |

D213 使用 `ARCH_RISCV64` + `ARCH_RISCV_FPU_D`；D126 使用 `ARCH_RISCV32` +
`ARCH_RISCV_FPU_S`。各板 `docs/` 保存内部开发资料（架构、启动、外设、
测试、上游化等），不随上游 BSP 提交，入口见各板的 `docs/README.md`。

## 命名约定

| 名称 | 含义 |
|---|---|
| `artinchip` | BSP 厂商目录 |
| `D21x`、`D12p` | SoC 系列 |
| `D213ECV`、`D126` | SoC 型号 |
| `d213ecv-demo-v4-0`、`d126fxr-demo-v1-1` | 实体开发板对应的 BSP 目录 |
| `demo128-nand`、`demo124-nor` | Luban-Lite 软件配置、分区和镜像产品名，不是开发板型号 |

`demo128-nand` 等名称保留在原厂路径（如 `target/d21x/demo128-nand/`、
`target/d12p/d126-demo124-nor/`）、分区配置和镜像文件名中。Kconfig 符号
`BOARD_ARTINCHIP_D213_DEMO128`、`BOARD_ARTINCHIP_D126_DEMO124_NOR`、
UART 启动横幅、`d126_info` 的 board 字段和
`--confirm-board d213-demo128` / `d126-demo124-nor` 属于既有接口，未随
目录改名调整。

## 目录结构

```text
bsp/artinchip/
├── libraries/                 公共 HAL 与设备适配层
│   ├── include/               D21x/D12p 共用寄存器描述与板级接口
│   ├── hal/i2c/               共享 I2C HAL
│   ├── drivers/               I2C、触摸、存储等 RT-Thread 设备适配
│   └── soc/d21x/              D21x 地址、时钟、原厂 HAL 与控制器驱动
├── d213ecv-demo-v4-0/         D213ECV-DEMO-V4-0 BSP
├── d126fxr-demo-v1-1/         D126FxR-DEMO-V1-1 BSP
└── tools/                     共享构建、检查与测试工具
```

## 公共层

`libraries/` 按 IP 复用边界组织：

- `include/`：D21x/D12p 布局一致的外设寄存器描述（偏移和位域），以及
  `aic_board.h` 板级只读描述符接口；
- `hal/i2c/`：与原厂 Luban-Lite 一致的 I2C HAL；
- `drivers/i2c/`：只依赖共享 HAL 的 RT-Thread I2C 总线适配，实例常量由
  SoC 层提供；
- `drivers/touch/`、`drivers/storage/`：GT911 触摸和 MTD/DFS 适配；
- `soc/d21x/`：D21x 专属地址、时钟/复位、原厂 HAL、显示、GE，以及
  UART/GPIO/RTC/WDT/DMA、音频、存储、USB/GMAC 等控制器适配。

D213 的 `board/` 只保留板级接线、供电、面板描述符、分区/挂载策略、
按键电阻梯和 CDC 描述符。板级 Kconfig 通过 `AIC_SOC_D21X`、
`AIC_USING_HAL_I2C`、`AIC_USING_DRV_I2C` 选择编译内容；D126 目前只接入
Kconfig/SCons/`scons --dist` 骨架，不编译 D21x 代码。

## 编译

工具链要求：

- RISC-V GNU 工具链，提供 `riscv64-unknown-elf-*`；
- Python 3、SCons、kconfiglib。

单独编译某个 BSP：

```sh
export RTT_ROOT=/path/to/rt-thread
export RTT_EXEC_PATH=/path/to/toolchain/bin
cd bsp/artinchip/d213ecv-demo-v4-0
scons -j8
```

D126 的编译方式相同，将目录换成 `d126fxr-demo-v1-1` 即可。具体的
工具链版本和配置项见各板 README。

批量构建并检查所有配置：

```sh
python bsp/artinchip/tools/build-check.py --targets d213-minimal d126-minimal
```

不指定 `--targets` 时检查全部 18 个配置。脚本会备份并恢复当前
`.config`/`rtconfig.h`，构建结果写入 `bsp/artinchip/artifacts/`（已被
Git 忽略）。需要生成可烧录镜像时再加 `--sdk /path/to/luban-lite`。

## 工具

| 文件 | 用途 |
|---|---|
| `tools/build-check.py` | 按配置批量构建、ELF 检查和打包 |
| `tools/check_elf.py` | 共享 ELF/map 检查器 |
| `tools/sdk_dist.py` | `scons --dist` 钩子，复制公共层并改写路径 |
| `tools/d213_hil.py`、`tools/d213_utest.py` | D213 构建/烧录/utest 自动化 |
| `tools/d126_hil.py`、`tools/d126_utest.py` | D126 串口回归与自动化 |

HIL 工具默认只读；烧录类操作需要显式确认板型，且不会擦写 eFuse 或安全
配置。Bootloader、PBP、DDR 初始化二进制来自本地 Luban-Lite SDK，不进入
本仓库。

## 许可证

BSP 自有代码使用 Apache-2.0。`libraries/` 中的原厂 HAL 和头文件来自
ArtInChip Luban-Lite（Apache-2.0），保留原始版权声明。RT-Thread 内核与
组件遵循仓库根目录的 `LICENSE`。

## 开发工具与分支

D213 构建、检查及打包辅助工具集中在 `tools/`：
`check_base_config.py --bsp <基础 BSP 路径>` 检查上游候选配置；
`d213_mk_firmware.py --elf <ELF> --sdk <SDK> --out <目录>` 打包上板镜像。
这些开发工具不随基础 BSP 提交上游；上游仅保留原生构建必需文件。

分支统一使用 `bsp/artinchip-d213-*`：`bsp/artinchip-d213-dev` 保留完整开发功能，
`bsp/artinchip-d213-base` 用于首个最小 BSP 上游候选。
