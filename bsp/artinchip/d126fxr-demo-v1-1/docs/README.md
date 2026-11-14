# ArtInChip D126FxR-DEMO-V1-1 内部开发文档

> 本目录是 `bsp/artinchip/d126fxr-demo-v1-1` 的内部开发资料，不随上游 BSP
> 提交。板级使用说明和当前支持状态以 [`../README.md`](../README.md) 为准。
> 文中带日期、HIL 报告编号或提交号的结论是历史记录，不代表当前 HEAD；
> 修改驱动或配置后应重新验证。

## 文档导航

| 文档 | 内容 |
|---|---|
| [01-board-mapping.md](01-board-mapping.md) | 板卡型号、官方页面与 Luban-Lite target 对应关系 |
| [02-architecture.md](02-architecture.md) | E907F、CLIC/CORET、PSRAM、启动和内存布局 |
| [03-build-package.md](03-build-package.md) | 编译、ELF 检查、打包和烧录 |
| [04-testing.md](04-testing.md) | 串口、tick、调度、IPC、内存、GPIO、utest 和 HIL |
| [05-ip-compatibility.md](05-ip-compatibility.md) | D21x/D12p IP 差异与复用边界 |
| [06-display-touch.md](06-display-touch.md) | MIPI DSI、触摸现状和后续计划 |
| [07-upstream-roadmap.md](07-upstream-roadmap.md) | 上游化和 D133 复用建议 |
| [08-provenance.md](08-provenance.md) | 原厂来源、许可证和配置对应关系 |

## 命名与状态

- 板卡：**D126FxR-DEMO-V1-1**，BSP 目录 `d126fxr-demo-v1-1/`；
- Luban-Lite target：`target/d12p/d126-demo124-nor`，保留在原厂路径、分区
  配置和镜像产品名中；
- 运行时 `d126_info` 的 `board` 字段、Kconfig 符号
  `BOARD_ARTINCHIP_D126_DEMO124_NOR` 和 HIL 的
  `--confirm-board d126-demo124-nor` 是兼容接口，未随目录改名调整。

带日期的实机结论记录的是当时提交的代码；目录规范化和后续修改不会自动
产生新的 HW 结论。当前支持状态以 [`../README.md`](../README.md) 为准。

## 重要事实

1. 本 BSP 使用 RT-Thread 官方 `libcpu/risc-v/common`，不维护独立的 BSP
   `cpu/` 目录。
2. 原厂 Bootloader、PBP 和 DDR 初始化资源由本地 Luban-Lite SDK 提供，
   不进入本仓库。
3. 线程 FPU 上下文保存是已知问题，`d126_selftest` 不作为启动验收项。
4. 显示、触摸、I2C、SPI/QSPI、WDT、RTC、PWM、CAN 和 SDIO Wi-Fi 尚未实现。
