# 19 维护与上游贡献

## 19.1 依赖版本

| 依赖 | 版本/来源 |
|---|---|
| RT-Thread | v5.3.0（本仓库分支 `bsp/artinchip-d21x`） |
| CPU Port | 上游 `libcpu/risc-v/common`（Next RISC-V） |
| 工具链 | Xuantie-900 elf newlib GCC V2.6.1（GCC 10.2.0） |
| SCons | 4.8.1（已验证） |
| Python | 3.13.15（已验证） |
| kconfiglib | 14.1.0 |
| LVGL | v9.6.0（外部 MIT 源码，固定 commit，`LVGL_DIR`） |
| Luban-Lite | 本地参考树，路径见 [20-vendor-source-license.md](20-vendor-source-license.md) |

## 19.2 原厂 SDK 兼容范围

本 BSP 的原厂 HAL 来自 D21x 分支的 Luban-Lite。兼容性假设：

- D21x `aic_clk_id.h` 的寄存器偏移/门控位不变；
- GPIO/I2C/CAN/PWM/WDT/RTC HAL API 不变；
- D21x 基地址/中断号不变；
- CMU 的 bit 12/13 语义不变。

如果原厂升级导致：

- 结构体字段变化；
- 函数签名变化；
- 时钟框架切换到 V2；
- D21x 头文件路径变化；

必须重新验证并更新兼容层。

## 19.3 HAL 升级流程

1. 记录当前 HAL 文件 SHA-256（见 20 章）；
2. 拉取新 SDK；
3. 对比 `vendor/hal` 与原始文件；
4. 如果原厂文件未变，只更新文档中的版本；
5. 如果变化：
   - 先替换 `vendor/hal`/`vendor/include`；
   - 编译最小/外设/全实例三种配置；
   - 检查新增 `#include`；
   - 在 `aic_compat.h` 增加最小原语；
   - 更新 SHA-256 和差异说明；
6. 实机回归；
7. 更新支持矩阵和测试记录。

**不要**直接把整个新 SDK 复制进来。

## 19.4 RT-Thread 主线兼容性

本 BSP 依赖的 RT-Thread 接口：

- `rt_hw_serial_register` / serial v1；
- `rt_i2c_bus_device_register` / `rt_i2c_transfer`；
- `rt_hw_can_register` / `struct rt_can_ops`（5.3 的 `sendmsg` 返回 `rt_ssize_t`）；
- `rt_device_pwm_register`；
- `rt_hw_watchdog_register`；
- `rt_hw_rtc_register`；
- `rt_device_pin_register`；
- `rt_hw_interrupt_install/umask/mask`；
- `INIT_BOARD_EXPORT` / `INIT_DEVICE_EXPORT`。

升级 RT-Thread 时重点检查：

1. CAN ops 是否新增回调（如 non-blocking）；
2. serial v1 是否仍可用；
3. pin ops 是否新增 `pin_irq_mode`/`pin_parse`；
4. RTC ops 是否新增 `get_timespec` 等；
5. device model 的 `rt_device_ops` 是否变化。

## 19.5 许可证

本 BSP 自有代码：Apache-2.0。

原厂 HAL/头文件：Apache-2.0，版权归 ArtInChip Technology Co., Ltd.。复制文件保留原始版权头。

RT-Thread 内核/组件：Apache-2.0（见仓库 `LICENSE`）。

不得：

- 删除原厂版权头；
- 把原厂 Apache-2.0 代码声明为私有；
- 引入 GPL/闭源代码到本 BSP 而不做隔离；
- 提交二进制固件/Bootloader（除非许可证明确允许且有记录）。

## 19.6 上游贡献注意事项

向 RT-Thread 上游提交前：

- [ ] 与 ArtInChip 确认 CLA/版权；
- [ ] 确认板卡资料可公开；
- [ ] 不包含原厂 Bootloader/PBP 二进制；
- [ ] 不包含本地 SDK 路径；
- [ ] 不包含生成物；
- [ ] 文档和代码一致；
- [ ] 默认 `.config` 可构建；
- [ ] 有实机启动日志；
- [ ] CI 可复现工具链；
- [ ] 代码风格符合 `documentation/7.contribution/`；
- [ ] 提交拆分清晰（BSP core、UART、GPIO、I2C、CAN、PWM/WDT/RTC、docs、packaging）。

当前阻碍：

- 重组后尚未实机回归；
- 外设尚未实机验证；
- CI 尚未固定工具链；
- 原厂 HAL 复用边界需要 maintainer 认可；
- 文档中的型号差异需要官方确认。

## 19.7 版本记录建议

每次发布/合并记录：

```text
BSP version:
RT-Thread commit:
Toolchain:
Luban-Lite reference:
Verified boards:
Verified peripherals:
Known issues:
Image size:
```

不要只写“优化/修复”。

## 19.8 分支策略

当前开发分支：`bsp/artinchip-d21x`。

建议：

- `main` 保持可构建；
- 功能分支按外设拆分；
- 不 force-push 公共分支；
- 不覆盖他人提交；
- 提交信息使用 `[bsp][artinchip]` 前缀；
- 一个提交只做一类改动。

## 19.9 未来工作

优先级建议：

1. GE 缩放/混合/旋转实机复测；
2. 显示/GE 压力测试与 Cache 一致性验证；
3. 重组后实机回归（UART0、Tick、FPU、GPIO）；
4. I2C/CAN/PWM/WDT/RTC 实机验证；
5. QSPI + SPI NAND + MTD + LittleFS；
6. LVGL 9.6.0 显示/触摸实机验收；
7. Ethernet + LwIP；
8. USB Host；
9. SDIO Wi-Fi；
10. 音频/视频/低功耗。

显示 LVDS/DE、硬件 Color Bar、双缓冲、VSync 和 GE FillRect/BitBlt 已于 2026-11-04 实机验证。

每项都必须先更新矩阵，再写代码，再做验证。
