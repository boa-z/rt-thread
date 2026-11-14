# 20 原厂源码复用与许可证记录

## 20.1 参考 SDK

本 BSP 统一跟踪 https://github.com/boa-z/luban-lite 的最新提交；本轮核对远端
HEAD 后固定审查基准为 V1.3.3 快照
`4060f85bd27a1eb555952921139b77cc41bdeb4e`，本地目录为
`/home/boa/Documents/luban-lite`。SDK 工作区保持只读。

原厂来源 `https://gitee.com/artinchip/luban-lite` 继续作为来源信息保留，
不作为独立更新目标，也不以其记录的短 commit 代替 GitHub 快照及实际源码检查。
旧版参考仓库不再作为当前基准。原厂文件保留版权及 SPDX 声明。

`bsp/artinchip/libraries/sdk_sources.json` 保存完整 SHA-256、快照提交和 exact/adapted/reference
分类。升级时先核对上述 GitHub 仓库最新提交、审查源码差异，再更新固定基准；
不能仅凭版本号或来源元数据认定已同步。执行：

```sh
python3 bsp/artinchip/tools/check_sdk_sources.py
python3 bsp/artinchip/tools/check_sdk_sources.py --sdk /path/to/luban-lite
```

第一条校验逐字节导入文件及适配文件存在性；第二条另校验 66 项 SDK 原文件。
适配文件允许保留本地修改，仍需人工审查。清单随 `scons --dist` 一同分发。

D21x 代码位于共享 `bsp/artinchip/libraries/`。下表散列按当前候选重新计算，
不能用旧记录代替实际文件比较。RTC alarm disable 和 GE 事务恢复等本地修改
单列于 20.5，不再标为与 SDK 字节一致。
下表“本地文件”列相对于 `bsp/artinchip/`。

## 20.2 直接复制的 HAL/头文件

以下文件与 SDK 原文件**逐字节一致**（SHA-256 前 32 位）：

| 本地文件 | 原厂路径 | SHA-256（前 32 位） |
|---|---|---|
| `libraries/soc/d21x/hal/aic_hal_can.c` | `bsp/artinchip/hal/can/aic_hal_can.c` | `5e7f0b9bd5bed63785aabb9a50003a78` |
| `libraries/soc/d21x/hal/aic_hal_gpio.c` | `bsp/artinchip/hal/gpio/aic_hal_gpio.c` | `ff09a9a4f2a7291e5f1f3cd2ceacdf27` |
| `libraries/hal/i2c/hal_i2c.c` | `bsp/artinchip/hal/i2c/hal_i2c.c` | `8b8aef0ce22e3af301115da3842cd81e` |
| `libraries/soc/d21x/hal/hal_pwm.c` | `bsp/artinchip/hal/pwm/hal_pwm.c` | `075425fa9b34df3f92409f15e040098a` |
| `libraries/soc/d21x/hal/hal_wdt.c` | `bsp/artinchip/hal/wdt/hal_wdt.c` | `8ded48963da723a53fd15f509c6a7d7a` |
| `libraries/soc/d21x/hal/hal_tsen.c` | `bsp/artinchip/hal/tsen/hal_tsen.c` | `0912d4ba68b6e22c86277195929ea219` |
| `libraries/soc/d21x/hal/hal_adcim.c` | `bsp/artinchip/hal/adcim/hal_adcim.c` | `9c7d8aae955a6c1864e9257b793a72e5` |
| `libraries/soc/d21x/hal/hal_gpai.c` | `bsp/artinchip/hal/gpai/hal_gpai.c` | `2903fadd2192048c41bbbede87bb7597` |
| `libraries/soc/d21x/hal/hal_efuse.c` | `bsp/artinchip/hal/efuse/hal_efuse.c` | `18dbd506b7460d7aa0469382108a4e57` |
| `libraries/soc/d21x/hal/hal_audio.c` | `bsp/artinchip/hal/audio/hal_audio.c` | `e17d6d79ce3412ace98c3c966a06973e` |
| `libraries/soc/d21x/hal/hal_ce.c` | `bsp/artinchip/hal/ce/hal_ce.c` | `3be5683380dc5338a5b6c3c9dc10ac95` |
| `libraries/soc/d21x/include/hal_ce.h` | `bsp/artinchip/include/hal/hal_ce.h` | `d92f17732f25bcd9f92959f218c79474` |
| `libraries/soc/d21x/include/hal_audio.h` | `bsp/artinchip/include/hal/hal_audio.h` | `9043cbdfd91c30145d92e526ac9a0bf2` |
| `libraries/soc/d21x/include/hal_audio_reg.h` | `bsp/artinchip/include/hal/hal_audio_reg.h` | `5c08f9674baab89d0d54d74b4918b2fc` |
| `libraries/soc/d21x/include/aic_hal_can.h` | `bsp/artinchip/include/hal/aic_hal_can.h` | `039d793e858521681be7709175ea9091` |
| `libraries/soc/d21x/include/aic_hal_gpio.h` | `bsp/artinchip/include/hal/aic_hal_gpio.h` | `fdc377411bced1d8a5bf92effbda0889` |
| `libraries/hal/i2c/hal_i2c.h` | `bsp/artinchip/include/hal/hal_i2c.h` | `0c16b8d478e3aa6c685ea9b29e6f3485` |
| `libraries/soc/d21x/include/hal_pwm.h` | `bsp/artinchip/include/hal/hal_pwm.h` | `9c97b278cd31680f58efe3bb2dac92b5` |
| `libraries/soc/d21x/include/hal_wdt.h` | `bsp/artinchip/include/hal/hal_wdt.h` | `06db1424a8003822bd9b848fea299f8e` |
| `libraries/soc/d21x/include/hal_tsen.h` | `bsp/artinchip/include/hal/hal_tsen.h` | `511251289e40ecc2d321fc93587b3e81` |
| `libraries/soc/d21x/include/hal_adcim.h` | `bsp/artinchip/include/hal/hal_adcim.h` | `3cadcc53bd28428e29a8321195c1d7c5` |
| `libraries/soc/d21x/include/hal_gpai.h` | `bsp/artinchip/include/hal/hal_gpai.h` | `53ab2376eafcfe06c7d77105194432cf` |
| `libraries/soc/d21x/include/hal_efuse.h` | `bsp/artinchip/include/hal/hal_efuse.h` | `01f3cf448b5fd2745b08f5462ef0455e` |
| `libraries/soc/d21x/include/aic_iopoll.h` | `bsp/common/include/aic_iopoll.h` | `0007dbefd9d7ce827c7436bda92847e0` |
| `libraries/soc/d21x/include/aic_clk_id.h` | `bsp/artinchip/sys/d21x/include/aic_clk_id.h` | `a7eafbf58b6dddb6d2dfc74cc9ce557b` |
| `libraries/soc/d21x/include/aic_gpio_id.h` | `bsp/artinchip/sys/d21x/include/aic_gpio_id.h` | `cbbd146ecc3f0923410e28b0e29983fd` |
| `libraries/soc/d21x/display/aic_hal_de.c` | `bsp/artinchip/hal/display/aic_hal_de.c` | `f2c77a70e74dae520323aef66c3ff85c` |
| `libraries/soc/d21x/include/aic_hal_de.h` | `bsp/artinchip/include/hal/aic_hal_de.h` | `bb52869b895f5490723674555f395beb` |
| `libraries/soc/d21x/include/aic_hal_disp_reg_util.h` | `bsp/artinchip/include/hal/aic_hal_disp_reg_util.h` | `d566e884d2cf62f9ecb3d7c40cc145bb` |
| `libraries/soc/d21x/include/aic_hal_lvds.h` | `bsp/artinchip/include/hal/aic_hal_lvds.h` | `adfae4943dcebc5cb65d6d2dfd6ec0c0` |
| `libraries/soc/d21x/include/mpp_types.h` | `bsp/artinchip/include/uapi/mpp_types.h` | `3ce10b8019efd4dd11667e6d4cb5c6f0` |
| `libraries/soc/d21x/ge/hal_ge_hw.c` | `bsp/artinchip/hal/ge/hal_ge_hw.c` | `acf91456238ee625e95bc11ecf56ba0c` |
| `libraries/soc/d21x/include/hal_ge_hw.h` | `bsp/artinchip/hal/ge/hal_ge_hw.h` | `bb88bf7510d34386082a4b4a11445f1c` |
| `libraries/soc/d21x/include/hal_ge_reg.h` | `bsp/artinchip/hal/ge/hal_ge_reg.h` | `88e2effa6602b238076df2747fdde24d` |
| `libraries/soc/d21x/include/aic_hal_ge.h` | `bsp/artinchip/include/hal/aic_hal_ge.h` | `18d64f6b882390a55768f31baf10e5af` |
| `libraries/soc/d21x/include/aic_drv_ge.h` | `bsp/artinchip/include/drv/aic_drv_ge.h` | `5f46a429776f6ef918691727fa11ebb6` |
| `libraries/soc/d21x/include/aic_reset_id.h` | `bsp/artinchip/sys/d21x/include/aic_reset_id.h` | `998461bfc83395cf7a1d37ba32860fbd` |

验证方法（Windows）：

```powershell
certutil -hashfile libraries\hal\i2c\hal_i2c.c SHA256
```

Linux：

```sh
sha256sum libraries/hal/i2c/hal_i2c.c
```

## 20.3 BSP 自有兼容文件

以下文件不是从 SDK 复制的，而是为裁剪依赖新写的：

| 文件 | 说明 | SHA-256（前 32 位） |
|---|---|---|
| `libraries/soc/d21x/include/aic_compat.h` | 类型/MMIO/日志/时间/错误码/中断返回码/OSAL shim/Cache+DMA 原语、对齐分配 shim、D21x 存储映射/IRQ、外设版本宏（含 GPAI） | `17b8c9af874bcaede174a7e546328c0d` |
| `libraries/soc/d21x/include/aic_common.h` | 兼容薄包装 | `c01c45717fde3a7fa5c6996236408cc2` |
| `libraries/soc/d21x/include/aic_core.h` | 兼容薄包装 | `94a1792c886cfeb1bbe46466ee7fe169` |
| `libraries/soc/d21x/include/aic_io.h` | 兼容薄包装 | `abebb104da962a00a5de67e9e536cbf4` |
| `libraries/soc/d21x/include/aic_log.h` | 兼容薄包装 | `7923b99f1aa188befdedfa0c780e30d8` |
| `libraries/soc/d21x/include/aic_errno.h` | 兼容薄包装 | `df3d94044fe059a43ef35beb9900bb70` |
| `libraries/soc/d21x/include/aic_utils.h` | 兼容薄包装（CE HAL 的未使用声明头） | `95ec79e8472b6fe32653ed382e964a30` |
| `libraries/soc/d21x/include/aic_hal_clk.h` | 精简时钟 API 声明 | `4b9106f865575169ae73f882449b88ba` |
| `libraries/soc/d21x/include/aic_hal.h` | 精简 HAL 聚合头 | `93181b7d4d7b911aeb0649ef12df81ed` |
| `libraries/soc/d21x/include/aic_hal_reset.h` | 精简复位 API 声明 | `7ae790db1819ce3373867137a9373cf9` |
| `libraries/soc/d21x/include/aic_osal.h` | OSAL 兼容包装 | `4a8dae5d6bc001380ca44dd27ac683ec` |
| `libraries/soc/d21x/include/aic_list.h` | GE 所需最小链表 | `e702496e8f796df81a8e5d58dda89178` |
| `libraries/soc/d21x/include/aic_disp_clk.h` | 显示时钟 API | `91218e3bf02dbf7e0a2bc7e9d2d76996` |
| `libraries/soc/d21x/port/aic_hal_clk.c` | D21x 门控/固定频率（含 CLK_DMA/TSEN/SID/ADCIM/GPAI/CE）、PLL_FRA1 与 Codec 分频 | `39fb0abdbcaa252f8dede030053a395f` |
| `libraries/soc/d21x/port/aic_disp_clk.c` | PLL_FRA2/SCLK/PIXCLK | `1795f454bc1e5614602abbe59d6641e3` |
| `libraries/soc/d21x/port/aic_hal_reset.c` | DE/GE/LVDS 复位 | `e6975b35adc712cd1e66c7ba6666e45c` |
| `libraries/include/aic_uart_reg.h` | UART 16550 偏移/LSR/LCR/FCR/MCR/USR 位域（D21x v1.0 与 D12p v1.4 共用） | `0aeb05ef27198408dae3d308ff2491d3` |
| `libraries/include/aic_gpio_reg.h` | GPIO group/pin 步长、GEN 寄存器和 `PIN_CFG` 位域（D21x/D12p 共用） | `b827ecf929df11bccb7a2d9b0db185af` |
| `libraries/SConscript` | 共享层条件编译（含 DMAC/HAL/TSEN/ADCIM/GPAI/eFuse/CE 与设备适配器） | `58b92857c45bc411fd4b6450b04ad634` |
| `libraries/Kconfig` | 共享层 SoC/IP 选择符号（`AIC_SOC_D21X`、`AIC_USING_HAL_I2C`、`AIC_USING_DRV_I2C`） | `42c3f41252896eab31a2b81bc1d2bd6d` |
| `tools/sdk_dist.py` | `scons --dist` 按 `.config` 选择性复制 SoC 层并改写路径 | `17c87006070f9362ef6216c24d4068bb` |
| `libraries/include/aic_board.h` | 只读板级描述符与 NAND 元数据校验 | `ed2aa6b070804a1aa245d3b763e880b5` |
| `libraries/soc/d21x/include/aic_soc.h` | 从板头提取的 D21x 地址和原始 IRQ | `170db674eb35c2e1a0e6b22d83d2d8ad` |

这些文件的许可证为 Apache-2.0，版权归 RT-Thread Development Team（2026）。

## 20.4 与 SDK 的差异

- `libraries/soc/d21x/hal`、`libraries/soc/d21x/display`、`libraries/soc/d21x/ge` 与 `libraries/soc/d21x/include` 中的上表原厂文件：直接复制文件按 20.2 逐项比较；RTC、GE、SDMC、DMA 等修改见 20.5；
- 没有复制原厂 `drv/*`、`aic_osal*`、`aic_drv*`、完整 CMU、PM、DTS、Luban-Lite 显示平台框架；
- `aic_compat.h` 用 RT-Thread API 映射 `aic_get_time_ms`、`udelay`、日志、GE OSAL 原语等；
- `aic_hal_clk.c` 只实现本 BSP 用到的 `hal_clk_*` 子集：外设频率是板级
  固定值，另外为音频实现了 PLL_FRA1 分数分频和 CLK_CODEC 分频；
- `aic_hal_reset.c` 实现 D21x 已启用显示、VE、USB、GMAC 等外设的复位子集；
- `artinchip_fb.h` 复用 SDK 的 Framebuffer UAPI，驱动仅实现其中列出的子集；
- 裁剪移植（非逐字节一致）的原厂模块见 20.5。

## 20.5 裁剪移植的原厂模块

以下文件由 Luban-Lite 裁剪而来，文件头保留了原厂版权和
`SPDX-License-Identifier: Apache-2.0`，并写明来源与修改范围。由于不是
逐字节一致，不参与 20.2 的哈希比对：

| 本地文件 | 原厂来源 | 修改范围 |
|---|---|---|
| `libraries/soc/d21x/include/artinchip_fb.h` | `bsp/artinchip/include/uapi/artinchip_fb.h` | 完整 UAPI，仅删除文件末尾空行；驱动支持范围见 13.18 |
| `libraries/soc/d21x/hal/hal_rtc.c` | `bsp/artinchip/hal/rtc/hal_rtc.c` | 增加 alarm disable API，支持取消闹钟及屏蔽中断 |
| `libraries/soc/d21x/include/hal_rtc.h` | `bsp/artinchip/include/hal/hal_rtc.h` | 增加 alarm disable API，支持取消闹钟及屏蔽中断 |
| `libraries/soc/d21x/ge/hal_ge_normal.c` | `bsp/artinchip/hal/ge/hal_ge_normal.c` | 事务错误传播、超时复位、初始化和释放资源清理（见 13.18）；原重复中断守卫已撤销（见 13.10） |
| `libraries/soc/d21x/hal/hal_sdmc.c` | `bsp/artinchip/hal/sdmc/hal_sdmc.c` | 去掉 GPL bouncebuf，增加 IDMA/软复位 |
| `libraries/soc/d21x/include/hal_sdmc.h` | `bsp/artinchip/include/hal/hal_sdmc.h` | 同上 |
| `libraries/soc/d21x/hal/hal_dma.c` | `bsp/artinchip/hal/dma/hal_dma_def_v1x.c` | 只保留 DMA v1.0；OSAL 由 `aic_core.h` 提供；`printf` 改 `rt_kprintf` |
| `libraries/soc/d21x/include/hal_dma.h` | `bsp/artinchip/include/hal/hal_dma.h` | 只保留 D21x/V10 接口 |
| `libraries/soc/d21x/include/hal_dma_reg.h` | `bsp/artinchip/hal/dma/hal_dma_reg_v1x.h` | 只保留 V10 分支和 D21x 通道表 |
| `libraries/soc/d21x/include/aic_dma_id.h` | `bsp/artinchip/sys/d21x/include/aic_dma_id.h` | 无逻辑修改 |

## 20.6 许可证文本

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
- 标明修改（20.5 中的裁剪移植文件已在文件头说明修改范围；20.2 的逐字节
  一致文件未修改）；
- 提供许可证副本；
- 不暗示原作者为你的修改背书。

本仓库根目录的 `LICENSE` 为 Apache-2.0。

## 20.7 未复制的原厂模块

以下模块没有进入本 BSP，因此不在本记录的许可证覆盖范围内：

```text
hal/canfd, drv/canfd
drv/dma (the v1.0 HAL only is ported, see 20.5)
hal/qspi, drv/qspi
hal/spinand, drv/spinand
drv/display (only the HAL + register definitions are used)
drv/ge (only the Normal HAL is used)
hal/ve, drv/ve
hal/usb, drv/usb (only the CherryUSB glue/port is used)
drv/sdmc (the SDMC HAL only is ported, see 20.5)
drv/mac (the GMAC/PHY HAL is in libraries/soc/d21x/hal/aic_mac*.c)
drv/audio (the Codec HAL is recorded in 20.2)
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

## 20.8 第三方/工具

`../tools/d213_mk_firmware.py` 调用本地 Luban-Lite 的 Python 工具。这些工具**不复制到本仓库**，由使用者在打包时提供。它们的许可证以 Luban-Lite 仓库为准。

本 BSP 另外逐字节复制了 LittleFS v2.5（`littlefs/lfs.c`、`lfs.h`、
`lfs_util.c`、`lfs_util.h`、`LICENSE.md`），来源为上游 littlefs
仓库，许可证 **BSD-3-Clause**，版权归 ARM Limited。源码保持原样；
BSP 只在 `littlefs/SConscript` 中为这一组定义 `LFS_NO_ASSERT`，
以规避上游 `lfs_mlist_isopen()` 仅在 `RT_DEBUG` 下编译的问题，
不修改导入文件。

## 20.9 SPI NAND / QSPI 参考实现

`../libraries/soc/d21x/drivers/drv_qspi.c` 与 `../libraries/soc/d21x/drivers/drv_spinand.c` 是**行为移植**，不是逐字节
复制。参考的 Luban-Lite 文件（均为 Apache-2.0）：

| 原厂路径 | 本 BSP 使用范围 |
|---|---|
| `bsp/artinchip/hal/qspi/qspi_hw_v1.0.h` | 寄存器偏移、位定义、FIFO 深度、水位 |
| `bsp/artinchip/hal/qspi/hal_qspi_v1x.c` | 初始化序列、同步 FIFO、TDONE/超时 |
| `bsp/artinchip/hal/qspi/qspi_internal.c` | CDR1/CDR2 分频、超时估算 |
| `bsp/artinchip/drv/qspi/drv_qspi.c` | 命令/地址/Dummy/数据阶段拆分、软件 CS |
| `bsp/peripheral/spinand/spinand.c` | RESET/READ ID/PAGE READ/READ CACHE、PROGRAM LOAD/EXECUTE、BLOCK ERASE、MARK BAD、ECC 状态、坏块读取 |
| `bsp/peripheral/spinand/winbond.c` | W25N01GV 等型号几何与 ECC 解码 |
| `bsp/peripheral/spinand/inc/spinand.h` | 命令字、寄存器、状态位 |
| `bsp/artinchip/drv/spinand/spinand_port.c` | `spi_nand_cmd_cfg` 到 `rt_qspi_message` 的映射 |
| `target/d21x/demo128-nand/pinmux.c` | PB.0~PB.5 功能 3 |

未使用、未链接：NFTL 静态库、`spinand_refresh`、UFFS、原厂 13 槽 MTD
扩展、DMA 驱动。若后续复制原厂文件，必须按 20.2 的方式补充 SHA-256、
依赖和修改记录。

## 20.10 合规检查清单

- [x] 原厂文件保留版权头；
- [x] 原厂文件未修改；
- [x] 兼容层标注 Apache-2.0；
- [x] 文档记录来源和 SHA-256；
- [x] 未提交 Bootloader/PBP/固件二进制；
- [x] 未提交本地 SDK 路径到构建脚本（仅文档示例）；
- [x] QSPI/SPI NAND 行为移植来源与许可证已记录；
- [x] LittleFS 版本、未修改状态和 BSD-3-Clause 许可证已记录；
- [ ] 上游贡献前由 ArtInChip/RT-Thread 复核；
- [ ] 新增 HAL 时更新本文件。

## 20.11 V1.3.3 迁移审查

| SDK 变化 | 本 BSP 处理 |
|---|---|
| 37 个逐字节复用的 HAL/头文件 | 36 个不变；同步 `hal_ce.h` 的 `hal_crypto_irq_disable()` 声明 |
| Winbond W25N02KV/KW 参数 | ECC 均修正为 8 bit/512B，KW 修正为 2048 blocks；无实机证据，仍不开放分区注册 |
| 出厂坏块标记 | 独立读取 OOB 标记，不再因主区 ECC 错误隐藏标记；保留忙超时和传输错误 |
| UI 背景混合 | 板级 Kconfig + owner API + 原有 IOCTL；默认关闭，保留 VSync 超时所有权保护 |
| bootloader concatenate | 使用 `{files, max_size}` 配置并限制 404 KiB；分区布局不变 |
| LVGL v9 GE cache 修正 | SDK 修正变换后区域；本 BSP 已按完整校验面同步 Cache，无需复制该 LVGL 文件 |
| GT911、VE、UART、QSPI | 审查现有路径；触摸数量检查、VE 生命周期已有防护；V15 UART 变化不适用于 D213 V10，不替换现有 QSPI polling |
| Codec / Middle Media / Player / Recorder | 独立 MPP 范围，本次不复制进 BSP |

`CODEMAP-v2.md` 是 v2.0 架构预告，描述 manifest、独立 HAL 与 D33x；
当前快照仍为 1.x 源码布局，没有可直接替换 D213 的 v2.0 实现。
本次保留已分离的板级描述符/共享驱动，不依据预告编造 v2.0 接口。
DVP/VIN、NFTL、量产写入可靠性以及新增显示路径仍需后续实现或上板验证。
