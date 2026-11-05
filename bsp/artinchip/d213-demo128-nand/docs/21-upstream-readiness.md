# 21 D21x BSP 上游化报告

## 21.1 基线与仓库关系

| 仓库 | 角色 | 当前策略 |
|---|---|---|
| `boa-z/rt-thread` `bsp/artinchip-d21x` | D21x BSP | 上游候选来源 |
| `boa-z/lvgl-aic-standalone` | 原生 RT-Thread LVGL 9.6 适配 | 外部组件，不进入 BSP 上游候选 |
| `boa-z/lvgl-aic` | Luban-Lite LVGL 适配 | 保持独立，不复用到 BSP 核心 |
| `lvgl/lvgl` v9.6.0 | 官方 GUI 核心 | 外部固定依赖，不提交源码 |

## 21.2 代码归属矩阵

| 代码 | 归属 | 上游候选处理 |
|---|---|---|
| `board/` 启动、时钟、PLIC、CLINT、Cache | BSP | 保留 |
| `drivers/drv_uart.c` / `drv_gpio.c` / `drv_i2c.c` | BSP | 保留 |
| `drivers/drv_can.c` / `drv_pwm.c` / `drv_wdt.c` / `drv_rtc.c` | BSP | 保留 |
| `drivers/drv_disp.c` / `drv_ge.c` | BSP | 保留，保持标准/公开接口 |
| `drivers/drv_touch_gt911.c` | BSP | 保留，只操作 RT-Thread Touch Device |
| `vendor/` 原厂 HAL | 第三方 | 保留原始版权/SPDX |
| `applications/` 自检和测试命令 | BSP 示例 | 可保留最小自检，业务示例不进入上游 |
| `lvgl/SConscript` | 外部组件集成 | 不进上游候选；由应用集成 |
| LVGL runtime/display/input/demo | `lvgl-aic-standalone` | 不进 BSP |
| Bootloader/PBP/DDR 资源 | 外部 SDK 资产 | 不进仓库 |
| `images/`、ELF/BIN/MAP | 构建产物 | 不提交 |

## 21.3 依赖关系

```text
RT-Thread BSP
  ├─ RT-Thread kernel / drivers / libcpu
  ├─ ArtInChip HAL (vendor/)
  └─ no LVGL at default build

lvgl-aic-standalone
  ├─ official LVGL v9.6.0 (external)
  ├─ RT-Thread kernel/device APIs
  └─ optional D21x public BSP APIs:
       drv_disp.h, drv_ge.h, board/cache.h

application/demo
  └─ lvgl-aic-standalone public API
```

## 21.4 上游候选文件范围

建议上游候选分支只包含：

- `bsp/artinchip/d213-demo128-nand/board/`
- `bsp/artinchip/d213-demo128-nand/drivers/`
- `bsp/artinchip/d213-demo128-nand/vendor/`
- `bsp/artinchip/d213-demo128-nand/applications/` 中最小自检/外设测试
- `bsp/artinchip/d213-demo128-nand/pack/` 的配置模板
- `bsp/artinchip/d213-demo128-nand/tools/mk_firmware.py`
- `bsp/artinchip/d213-demo128-nand/SConstruct`、`SConscript`、`Kconfig`、
  `rtconfig.py`、`.config`、`rtconfig.h`
- 文档、许可证和来源说明

明确排除：

- `lvgl/` 集成与任何 LVGL 源码/配置；
- 固件镜像、ELF、BIN、MAP、build 目录；
- Bootloader.bin、PBP、DDR 二进制资源；
- 业务应用和客户页面。

## 21.5 候选 PR 拆分

1. `[bsp][artinchip] add D213 M-mode startup, PLIC and CLINT`
2. `[bsp][artinchip] add D213 UART, GPIO and I2C`
3. `[bsp][artinchip] add D213 CAN, PWM, WDT and RTC`
4. `[bsp][artinchip] add D213 LVDS/DE framebuffer and GE Normal`
5. `[bsp][artinchip] add D213 GT911 touch`
6. `[bsp][artinchip] add D213 docs, packaging and validation`

组件相关提交留在 `lvgl-aic-standalone`，不混入 BSP PR。

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

| 项目 | 状态 |
|---|---|
| 默认 BSP（无 LVGL）构建 | BUILD PASS |
| BSP + lvgl-aic-standalone 构建 | BUILD PASS（GCC 16.1.0） |
| Host 逻辑测试 | HOST PASS |
| 固件打包 | PACKAGE PASS |
| 新 standalone 镜像实机 | PENDING |
| GE Draw Unit | PENDING |
| GE 帧拷贝实机 | PENDING |

## 21.8 遗留问题

- `lvgl-aic-standalone` 新镜像需要重新做显示/触摸实机验收；
- GE Draw Unit 尚未实现，当前只有可选 GE 帧拷贝；
- SPI NAND/MTD/文件系统、MIPI/RGB、USB、SDIO、Ethernet 仍未实现；
- Bootloader/PBP/DDR 资源依赖外部 Luban-Lite SDK；
- 上游是否接受 vendor HAL 目录和打包脚本仍需维护者确认。

## 21.9 后续维护建议

1. 保持 BSP 默认配置不依赖 LVGL/LVGL_AIC。
2. 每个 BSP 驱动变更都跑无 LVGL 构建。
3. 图形变更在 standalone 仓库独立测试和发布。
4. LVGL 升级只改外部固定版本与组件兼容性，不修改官方源码。
5. 新增板卡优先复用 `drivers/`，板级差异留在 `board/` 和 `pack/`。
