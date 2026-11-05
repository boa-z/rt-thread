# 20 原厂源码复用与许可证记录

## 20.1 参考 SDK

本 BSP 的原厂代码来自本地 Luban-Lite D21x 工作树：

仓库地址（公开参考）：

```text
https://github.com/boa-z/luban-lite
```

由于本地工作树可能包含未提交修改，本文记录的是**实际复制时的文件 SHA-256**，而不是某个 Git 提交号。复制文件均保留了原始版权头和 `SPDX-License-Identifier: Apache-2.0`。

## 20.2 直接复制的 HAL/头文件

以下文件与 SDK 原文件**逐字节一致**（SHA-256 前 32 位）：

| 本地文件 | 原厂路径 | SHA-256（前 32 位） |
|---|---|---|
| `vendor/hal/aic_hal_can.c` | `bsp/artinchip/hal/can/aic_hal_can.c` | `b2c6cabfad3d6a9c28de4e0bdd9beee4` |
| `vendor/hal/aic_hal_gpio.c` | `bsp/artinchip/hal/gpio/aic_hal_gpio.c` | `7368740dcd47ae96a8d4c98d9024bd6e` |
| `vendor/hal/hal_i2c.c` | `bsp/artinchip/hal/i2c/hal_i2c.c` | `f36d71d799f616946578058e752b54ee` |
| `vendor/hal/hal_pwm.c` | `bsp/artinchip/hal/pwm/hal_pwm.c` | `c3c3ed98ff53481b812f9e3d43b17fc0` |
| `vendor/hal/hal_rtc.c` | `bsp/artinchip/hal/rtc/hal_rtc.c` | `e129706b42d58f70ebd5327a9c3678a5` |
| `vendor/hal/hal_wdt.c` | `bsp/artinchip/hal/wdt/hal_wdt.c` | `57862446dd22c790bfe3c49bdeabefea` |
| `vendor/include/aic_hal_can.h` | `bsp/artinchip/include/hal/aic_hal_can.h` | `28732192a7735c1c958691aca6030f29` |
| `vendor/include/aic_hal_gpio.h` | `bsp/artinchip/include/hal/aic_hal_gpio.h` | `2c2f01d1b219490ce5634dd66041a62e` |
| `vendor/include/hal_i2c.h` | `bsp/artinchip/include/hal/hal_i2c.h` | `7a92d351174e7dd615ffdb385507b274` |
| `vendor/include/hal_pwm.h` | `bsp/artinchip/include/hal/hal_pwm.h` | `37b19e4b0106a2a6fa12d4ec829e8c5e` |
| `vendor/include/hal_rtc.h` | `bsp/artinchip/include/hal/hal_rtc.h` | `e475e76d88c959333cb187d4752b15dd` |
| `vendor/include/hal_wdt.h` | `bsp/artinchip/include/hal/hal_wdt.h` | `7fc7d7f20489399d005619842ecc6ef7` |
| `vendor/include/aic_clk_id.h` | `bsp/artinchip/sys/d21x/include/aic_clk_id.h` | `64c2c01642561fd472fe04407537de42` |
| `vendor/include/aic_gpio_id.h` | `bsp/artinchip/sys/d21x/include/aic_gpio_id.h` | `4f965526cc8364b3a747d0b0c1cf6345` |
| `vendor/display/aic_hal_de.c` | `bsp/artinchip/hal/display/aic_hal_de.c` | `b6cf581a9830e497c8ea194be44b9039` |
| `vendor/include/aic_hal_de.h` | `bsp/artinchip/include/hal/aic_hal_de.h` | `375a5ab2199a5582c130c21ec1035360` |
| `vendor/include/aic_hal_disp_reg_util.h` | `bsp/artinchip/include/hal/aic_hal_disp_reg_util.h` | `be461619e7b1e593950d7c9c5965f821` |
| `vendor/include/aic_hal_lvds.h` | `bsp/artinchip/include/hal/aic_hal_lvds.h` | `f5311725fe7ecad0a2227761d980c585` |
| `vendor/include/mpp_types.h` | `bsp/artinchip/include/uapi/mpp_types.h` | `139939ba6f95d64e1901137f6c771dfc` |
| `vendor/ge/hal_ge_normal.c` | `bsp/artinchip/hal/ge/hal_ge_normal.c` | `8d86e10079f5161e9274ef08d97fcf56` |
| `vendor/ge/hal_ge_hw.c` | `bsp/artinchip/hal/ge/hal_ge_hw.c` | `1dbf2d64b67bfe6e185cf67ae1e6196e` |
| `vendor/include/hal_ge_hw.h` | `bsp/artinchip/hal/ge/hal_ge_hw.h` | `713d286a3dfb42b6e35b8d05b788fc85` |
| `vendor/include/hal_ge_reg.h` | `bsp/artinchip/hal/ge/hal_ge_reg.h` | `2d245c9d81e4210d35dab39050d22dc7` |
| `vendor/include/aic_hal_ge.h` | `bsp/artinchip/include/hal/aic_hal_ge.h` | `5cdf414dd39c2beda3672e4b50308125` |
| `vendor/include/aic_drv_ge.h` | `bsp/artinchip/include/drv/aic_drv_ge.h` | `684c8233086e075f5a831bc5bfda693c` |
| `vendor/include/aic_reset_id.h` | `bsp/artinchip/sys/d21x/include/aic_reset_id.h` | `8434aa939cab084dd26dc3dfd73d6e7f` |

验证方法（Windows）：

```powershell
certutil -hashfile vendor\hal\hal_i2c.c SHA256
```

Linux：

```sh
sha256sum vendor/hal/hal_i2c.c
```

## 20.3 BSP 自有兼容文件

以下文件不是从 SDK 复制的，而是为裁剪依赖新写的：

| 文件 | 说明 | SHA-256（前 32 位） |
|---|---|---|
| `vendor/include/aic_compat.h` | 类型/MMIO/日志/时间/错误码/中断返回码/OSAL shim | `0479dda318dc198a5194cd4bc932fe3e` |
| `vendor/include/aic_common.h` | 兼容薄包装 | `c01c45717fde3a7fa5c6996236408cc2` |
| `vendor/include/aic_core.h` | 兼容薄包装 | `94a1792c886cfeb1bbe46466ee7fe169` |
| `vendor/include/aic_io.h` | 兼容薄包装 | `abebb104da962a00a5de67e9e536cbf4` |
| `vendor/include/aic_log.h` | 兼容薄包装 | `7923b99f1aa188befdedfa0c780e30d8` |
| `vendor/include/aic_errno.h` | 兼容薄包装 | `df3d94044fe059a43ef35beb9900bb70` |
| `vendor/include/aic_hal_clk.h` | 精简时钟 API 声明 | `4b9106f865575169ae73f882449b88ba` |
| `vendor/include/aic_hal.h` | 精简 HAL 聚合头 | `93181b7d4d7b911aeb0649ef12df81ed` |
| `vendor/include/aic_hal_reset.h` | 精简复位 API 声明 | `7ae790db1819ce3373867137a9373cf9` |
| `vendor/include/aic_osal.h` | OSAL 兼容包装 | `4a8dae5d6bc001380ca44dd27ac683ec` |
| `vendor/include/aic_list.h` | GE 所需最小链表 | `e702496e8f796df81a8e5d58dda89178` |
| `vendor/include/aic_disp_clk.h` | 显示时钟 API | `91218e3bf02dbf7e0a2bc7e9d2d76996` |
| `vendor/include/artinchip_fb.h` | FB 常量兼容头 | `b71dd5f8a35a8ed70d5a27160d5a2c17` |
| `vendor/port/aic_hal_clk.c` | D21x 门控/固定频率 | `9bba25b9f880ce781a044c5df6ec8134` |
| `vendor/port/aic_disp_clk.c` | PLL_FRA2/SCLK/PIXCLK | `1795f454bc1e5614602abbe59d6641e3` |
| `vendor/port/aic_hal_reset.c` | DE/GE/LVDS 复位 | `2181a88344fa4ea1cceba38ef5b28457` |
| `vendor/SConscript` | 条件编译 | `b7bfd86aed4d5a12ec4e01a09fbc4a0c` |

这些文件的许可证为 Apache-2.0，版权归 RT-Thread Development Team（2026）。

## 20.4 与 SDK 的差异

- `vendor/hal`、`vendor/display`、`vendor/ge` 与 `vendor/include` 中的上表原厂文件：**无差异**；
- 没有复制原厂 `drv/*`、`aic_osal*`、`aic_drv*`、完整 CMU、DMA、PM、DTS、Luban-Lite 显示平台框架；
- `aic_compat.h` 用 RT-Thread API 映射 `aic_get_time_ms`、`udelay`、日志、GE OSAL 原语等；
- `aic_hal_clk.c` 只实现本 BSP 用到的 `hal_clk_*` 子集，频率是板级固定值；
- `aic_hal_reset.c` 只实现 DE/GE/LVDS/RGB/MIPI 复位；
- `artinchip_fb.h` 是 BSP 自有兼容头，不是原厂完整 UAPI 头。

## 20.5 许可证文本

原厂文件头示例：

```c
/*
 * Copyright (c) 2022-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */
```

Apache-2.0 允许复制、修改和再分发，但要求：

- 保留版权/许可证声明；
- 标明修改（本 BSP 未修改原厂 HAL 文件）；
- 提供许可证副本；
- 不暗示原作者为你的修改背书。

本仓库根目录的 `LICENSE` 为 Apache-2.0。

## 20.6 未复制的原厂模块

以下模块没有进入本 BSP，因此不在本记录的许可证覆盖范围内：

```text
hal/canfd, drv/canfd
hal/dma, drv/dma
hal/qspi, drv/qspi
hal/spinand, drv/spinand
drv/display (only the HAL + register definitions are used)
drv/ge (only the Normal HAL is used)
hal/ve, drv/ve
hal/usb, drv/usb
hal/sdmc, drv/sdmc
hal/mac, drv/mac
hal/audio, drv/audio
hal/psadc, drv/psadc
...
```

如果未来复制，必须逐项补充：

- 源路径；
- SHA-256；
- 依赖；
- 许可证；
- 修改记录；
- 验证状态。

## 20.7 第三方/工具

`tools/mk_firmware.py` 调用本地 Luban-Lite 的 Python 工具。这些工具**不复制到本仓库**，由使用者在打包时提供。它们的许可证以 Luban-Lite 仓库为准。

## 20.8 合规检查清单

- [x] 原厂文件保留版权头；
- [x] 原厂文件未修改；
- [x] 兼容层标注 Apache-2.0；
- [x] 文档记录来源和 SHA-256；
- [x] 未提交 Bootloader/PBP/固件二进制；
- [x] 未提交本地 SDK 路径到构建脚本（仅文档示例）；
- [ ] 上游贡献前由 ArtInChip/RT-Thread 复核；
- [ ] 新增 HAL 时更新本文件。
