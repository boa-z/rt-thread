# 21 D21x BSP 上游化报告

## 21.1 基线与仓库关系

| 仓库 | 角色 | 当前策略 |
|---|---|---|
| `boa-z/rt-thread` `bsp/artinchip-d213-dev` | D21x BSP | 上游候选来源 |
| `boa-z/lvgl-aic-standalone` | 原生 RT-Thread LVGL 9.6 适配 | 外部组件，不进入 BSP 上游候选 |
| `boa-z/lvgl-aic` | Luban-Lite LVGL 适配 | 保持独立，不复用到 BSP 核心 |
| `lvgl/lvgl` v9.6.0 | 官方 GUI 核心 | 外部固定依赖，不提交源码 |

## 21.2 代码归属矩阵

| 代码 | 归属 | 上游候选处理 |
|---|---|---|
| `board/` common-port glue、时钟、PLIC quirk、Cache、linker | BSP | 保留 |
| `../libraries/soc/d21x/drivers/drv_uart.c` / `drv_gpio.c` / `drv_rtc.c` / `drv_wdt.c` / `drv_dma.c` | BSP（D21x 共享层） | 保留 |
| `../libraries/drivers/i2c/drv_i2c.c` | BSP（跨 SoC 共享层） | 保留 |
| `../libraries/soc/d21x/drivers/drv_can.c` / `drv_pwm.c` | BSP | 保留 |
| `../libraries/soc/d21x/drivers/drv_disp.c` / `drv_ge.c` | BSP | 保留，保持标准/公开接口 |
| `../libraries/drivers/touch/drv_touch_gt911.c` | BSP | 保留，只操作 RT-Thread Touch Device |
| `../libraries/` 原厂 HAL 与共享层 | 第三方 + BSP | 保留原始版权/SPDX，共享层按 IP 复用边界拆分 |
| `applications/` 自检和测试命令 | BSP 示例 | 可保留最小自检，业务示例不进入上游 |
| `lvgl/SConscript` | 外部组件集成 | 不进上游候选；由应用集成 |
| LVGL runtime/display/input/demo | `lvgl-aic-standalone` | 不进 BSP |
| Bootloader/PBP/DDR 资源 | 外部 SDK 资产 | 不进仓库 |
| `images/`、ELF/BIN/MAP | 构建产物 | 不提交 |

## 21.3 依赖关系

```text
RT-Thread BSP
  ├─ RT-Thread kernel / drivers / libcpu
  ├─ ArtInChip HAL (../libraries/)
  └─ no LVGL at default build

lvgl-aic-standalone
  ├─ official LVGL v9.6.0 (external)
  ├─ RT-Thread kernel/device APIs
  └─ optional D21x public BSP APIs:
       drv_disp.h, drv_ge.h, board/aic_cache.h

application/demo
  └─ lvgl-aic-standalone public API
```

## 21.4 上游候选文件范围

建议上游候选分支只包含：

- `bsp/artinchip/d213ecv-demo-v4-0/board/`
- `bsp/artinchip/libraries/`
- `bsp/artinchip/d213ecv-demo-v4-0/applications/` 中最小自检/外设测试
- `bsp/artinchip/d213ecv-demo-v4-0/pack/` 的配置模板
- `bsp/artinchip/tools/d213_mk_firmware.py`
- `bsp/artinchip/d213ecv-demo-v4-0/SConstruct`、`SConscript`、`Kconfig`、
  `rtconfig.py`、`.config`、`rtconfig.h`
- 文档、许可证和来源说明

明确排除：

- `lvgl/` 集成与任何 LVGL 源码/配置；
- 固件镜像、ELF、BIN、MAP、build 目录；
- Bootloader.bin、PBP、DDR 二进制资源；
- 业务应用和客户页面。

## 21.5 上游候选提交序列

以 `origin/master` 的 `6c955645ec82372469f20746ead53e2ce3b5eda5` 为基线，
将开发过程中的导入、修复、迁移和验证记录整理为以下六个依次依赖的提交。
该序列是提交上游时的建议审查拆分，不是当前 `bsp/artinchip-d213-dev` 分支
的实际提交历史；该分支已按共享层、D213、D126、工具和文档合并为五个提交。

| 顺序 | 提交主题 | 审查范围 |
|---|---|---|
| 1 | `[bsp][artinchip] add D21x HAL from Luban-Lite V1.3.2` | HAL、寄存器、UAPI 及最小 RT-Thread 兼容层，保留原厂版权 |
| 2 | `[bsp][artinchip] add shared device adapters and build helpers` | 共享 RT-Thread 设备适配、Kconfig/SCons 与 Build ID/dist/ELF 工具 |
| 3 | `[bsp][artinchip] add D213ECV-DEMO-V4-0 NAND board support` | C906 板级初始化、外设、存储、多媒体、示例及配置 |
| 4 | `[bsp][artinchip] add D126 demo124 NOR board support` | 原有 E907 板级实现，保持内容不变 |
| 5 | `[bsp][artinchip] add board regressions and host validation tools` | 板端 utest、测试配置、主机测试和可选 HIL 工具 |
| 6 | `[bsp][artinchip] document boards and SDK provenance` | 使用说明、许可证、SDK 对照与历史硬件证据 |

当前源码树基于 RT-Thread master `6c955645ec82372469f20746ead53e2ce3b5eda5`
（RT-Thread 5.4.0 开发线，2026-11-12）。HAL 已按 20.11 对齐到 Luban-Lite
V1.3.3 快照 `4060f85bd27a1eb555952921139b77cc41bdeb4e`；提交 1 的主题保留
最早的 V1.3.2 导入记录，复核时应以当前文件 SHA-256 清单为准。板级目录
统一为 `d213ecv-demo-v4-0/`（实体开发板 D213ECV-DEMO-V4-0），原厂软件
配置名 `demo128-nand` 和镜像产品名保留不变，见 ADR-026。

此序列只整理现有功能的审查边界，不把过程中的错误版本和重复记录作为
独立提交保留，也不自动向官方提交 PR。所有提交仅位于 `bsp/artinchip/`。

参考 RT-Thread `documentation/7.contribution/bsp_contribution_guide_cn.md`、
主线 `bsp/hpmicro` 和 `bsp/bouffalo_lab`：`board/` 保留板级初始化，
`libraries/` 保留复用 HAL/适配层，`tools/` 保留厂商共享工具。
SDK 的 `hal/`、`drv/`、`sys/d21x/`、`include/uapi/` 分别对应这里的 HAL、
RT-Thread 设备适配、D21x 平台 glue 和公开硬件类型；不复制 SDK 构建框架。

D213 使用 `RT_USING_RISCV_NEW_COMMON`。FPU 和 PLIC 修复已由官方 master
提供，本提交序列不修改 `libcpu/`。公共 CherryUSB 修改也已从 BSP 分支移除。

CherryUSB 官方最新 AIC port 更新为
[`a0054795c440`](https://github.com/cherry-embedded/CherryUSB/commit/a0054795c44037ace8a2b77b6e610adbff2257ea)。
当前 RT-Thread 内置 port 尚未同步其 bus-id API 等适配，USB0 Device 的
两个配置（usbdev-only、usbdev-utest）的 Device 路径须等待该公共
依赖更新；不以 BSP 私有补丁替代。USB Host 不依赖这两处 Device port 修改。
历史 USB Device 硬件报告使用了当时的本地 port 修改，不代表当前候选通过。

D213 的控制器适配现已迁入共享层；板级接线、面板与供电、分区/挂载、按键和
CDC 描述符通过 `aic_board.h` 与控制器分离，详见 2.11。板内 LittleFS 副本及
可选 LVGL hook 仍应与维护者确认集成方式。软件构建不替代当前版本的实机回归。

## 21.6 许可证与第三方依赖

| 内容 | 许可证/来源 | 处理 |
|---|---|---|
| RT-Thread BSP 新代码 | Apache-2.0 | 保留 SPDX/版权 |
| ArtInChip HAL | Apache-2.0，原厂版权 | 逐字节保留原文件声明 |
| LVGL | MIT | 外部依赖，不提交 |
| lvgl-aic-standalone | Apache-2.0 | 独立仓库 |
| lvgl-aic | 按其仓库声明 | 不复制 Luban-Lite 专用代码 |
| Bootloader/PBP | 原厂资产 | 外部提供，不提交 |

没有把闭源预编译文件或来源不明资源加入候选 BSP。

## 21.7 验证结果

2026-11-13 板级目录改名后的复核（工具链 Xuantie-900 V3.3.0 / GCC 14.3.0，
Python 3.13.15，SCons 4.8.1；无 Luban-Lite SDK，未打包、未上板）：

| 项目 | 结果 |
|---|---|
| D213 minimal | BUILD PASS；ELF/map CHECK PASS（text 65664 / data 2616 / bss 14232） |
| D213 default | BUILD PASS；ELF/map CHECK PASS（text 132144 / data 6408 / bss 15912） |
| D213 utest | BUILD PASS；ELF/map CHECK PASS（text 361360 / data 7616 / bss 24632） |
| `scons --dist`（minimal） | 分发树生成 PASS；独立 BUILD PASS；共享检查器对分发 ELF CHECK PASS |
| `build-check.py` 路径解析 | 18 个 target 的 BSP 目录全部存在；D213 统一使用 `D213_BSP` 常量 |
| D126 minimal | Kconfig/SCons 路径解析 PASS；BUILD 被既有 GCC 14.3 错误阻塞：`board/reboot.c:113` 的 `INIT_BOARD_EXPORT` 返回类型不兼容。已在未改动的 HEAD `190211755c` 复现，与本次改名无关；D126 代码未修改 |
| 主机测试 | 74 项中 73 PASS；`test_sdk_sources.test_repository_imports` 因 Windows 检出 CRLF 与 LF 账本哈希不一致而失败，属既有环境问题，文件内容与 HEAD 一致 |
| 固件打包 | 未运行：本机无匹配的 Luban-Lite SDK 二进制资产 |
| 实机 | 未运行：本次无开发板验证，也不把 2026-11-06/09 的历史报告视为本次结果 |

历史候选验证（移除公共 USB 修改后）保留如下，不能代替本次改名复核：

| 项目（历史） | 结果 |
|---|---|
| D213 minimal / default / utest / USB Host | 完整 BUILD + ELF CHECK PASS |
| D126 minimal / utest | 完整 BUILD + ELF CHECK PASS，板级源码不改 |
| D213 USB0 Device only / utest | BUILD FAIL：内置 AIC port 的旧 bus-id API，等待公共依赖同步 |
| D213 Buffer / VE 主机测试 | HOST PASS，ASan + UBSan PASS |
| 主机 HIL / 串口结果分类等测试 | HOST PASS（使用测试替身，不是实机 HIL） |
| 固件打包 | 本轮未运行；无 SDK 二进制资产输入 |
| 当前候选实机 | 未验证，无开发板 |

此前的多配置打包、LVGL 和外设 HIL 报告保留在相应章节作为历史证据，不能
代替本次候选验证。最终向官方发 PR 前，需解决 21.5 的公共依赖和目录审查项，
并按可用硬件补做运行回归。此次仅整理用户仓库，不创建官方 PR。

## 21.8 遗留问题

- `lvgl-aic-standalone` 新镜像需要重新做显示/触摸实机验收；
- GE Draw Unit 尚未实现，当前只有可选 GE 帧拷贝；
- SPI NAND 的读写 MTD 路径已完成（读取 HW PASS；Program/Erase/Move/Mark-Bad
  在 `nand_scratch` HW PASS，HIL `20261108-230606-16252`），`/rodata`
  只读 FAT 与 `/nand` LittleFS + 持久化 BBT 也已 HW PASS（HIL
  `20261109-002129-38512`），OTA/A-B 只读诊断亦 HW PASS（HIL
  `20261109-015923-59712`），分区/FIT MSH 输出、BBT 双槽切换和看门狗
  复位注入 HW PASS（报告 `20261109-035532`），但 NFTL 和 OTA 更新
  引擎仍未实现；
- USB Host（USB1 EHCI/OHCI + MSC/FAT）、SDIO 主机（RTL8733BS 枚举/CMD52
  实机通过，CMD53 受厂方限制）和 Ethernet（GMAC0 RMII + RTL8201F）已实现
  并实机验证；USB0 Device（CDC ACM，`usb-acm0`）枚举、双向数据和
  `usb_dev_console on/off` 切换（16 轮）已实机通过；加密引擎
  （CE，标准 hwcrypto，AES/MD5/SHA-1/SHA-2）已实机 5/5 通过
  （HIL `20261109-034918-80696`）；USB Device 为旧 port 基线的历史结果，当前依赖见 21.5；USB OTG 自动角色切换未实现；
- MIPI/RGB 仍未实现；
- Bootloader/PBP/DDR 资源依赖外部 Luban-Lite SDK；
- 上游是否接受 vendor HAL 目录和打包脚本仍需维护者确认。

## 21.9 后续维护建议

1. 保持 BSP 默认配置不依赖 LVGL/LVGL_AIC。
2. 每个 BSP 驱动变更都跑无 LVGL 构建。
3. 图形变更在 standalone 仓库独立测试和发布。
4. LVGL 升级只改外部固定版本与组件兼容性，不修改官方源码。
5. 新增板卡优先复用 `libraries/` 中的控制器与设备适配，板级差异留在 `board/` 和 `pack/`。

## 21.10 驱动与板级配置分离回归（前一阶段）

本轮仅修改 `bsp/artinchip/`，不改 D126 板级源码、原厂 HAL、公共 USB 或
`libcpu/`，不依赖 MPP、CarPlay 或 Luban-Lite 的构建环境。

- `build-check.py`：D213 minimal/default/nand-readonly、sdcard-utest、
  sdio-utest、audio-utest、eth-utest、ce-utest、usb-utest、uart-dma-utest、
  utest 均通过构建和 ELF 检查；D126 minimal/utest 回归通过。
- 额外编译：default + VE；800×480 面板、替代面板/Touch/SDIO 引脚；
  移开本地 MPP 链接后仍能构建。
- D213 utest 和 D126 minimal 的 `scons --dist` 分发包在临时独立目录构建通过；
  分发包携带独立 Build ID 生成器，D126 发布树不包含 D21x SoC 层。
- 共享工具 72 项主机测试、NAND 分区生成器 4 项测试通过；Buffer、实际 VE
  驱动及 NAND 板级布局校验通过 ASan/UBSan。布局覆盖重叠、越界/溢出、
  重名、scratch 冲突、非法几何和替代板卡布局。

这些结果仅为软件验证，没有当前硬件验证。仍需上板回归 LVDS/LVGL、音频
功放、SD/SDIO 上电与采样、NAND 读写/挂载及 USB Host。USB0 Device 仍有
21.5 所述公共 CherryUSB API 依赖阻塞。本轮未执行固件打包或烧录。

## 21.11 V1.3.3 快照迁移回归

跟踪仓库统一为 `https://github.com/boa-z/luban-lite`，本轮核对最新 HEAD
`4060f85bd27a1eb555952921139b77cc41bdeb4e`；Gitee URL 仅保留原厂来源。
66 项源文件清单和实际 SDK 校验通过，迁移决策见 20.11。

- 13 个配置通过交叉编译和 ELF 检查：11 个 D213 配置，D126 minimal/utest。
- 移开 MPP checkout 后，default + VE、800×480 替代板级配置 + UI 背景混合
  开启均构建通过；D213 utest 独立分发包构建和主机驱动测试通过。
- 共享工具 74 项、NAND 分区生成器 4 项测试通过；Buffer、VE、板级布局和
  实际 SPI NAND 驱动通过 ASan/UBSan（QSPI/RT-Thread 为测试替身）。
- 用 V1.3.3 原厂脚本实际生成 IMG；bootloader 为 412688 B，小于 404 KiB
  上限。将上限改为 1 KiB 的负例确认脚本拒绝超限。

仅完成软件验证，没有新增实机结果。首先需回归 W25N01GV 坏块/数据页读取、
LVDS/LVGL 和背景混合/所有权交接，再扩展 KV/KW 芯片验证；USB Device 的
公共 API 依赖仍见 21.5。v2.0 仅有架构预告，本 BSP 未声明支持 D213 v2.0。
