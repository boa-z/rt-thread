# ArtInChip D21x RT-Thread BSP 文档中心

本目录是 `bsp/artinchip/d213-demo128-nand` 的完整开发文档。文档面向三类读者：

- 第一次拿到 D213 demo128-nand 板卡、希望把 BSP 编译并运行起来的工程师；
- 需要在 BSP 上增加外设、文件系统、显示或网络功能的驱动工程师；
- 需要长期维护本 BSP、评估上游贡献或升级原厂 HAL 的维护者。

## 文档导航

| 文档 | 内容 |
|---|---|
| [01-overview.md](01-overview.md) | 项目定位、支持范围、目录结构、快速上手 |
| [02-architecture.md](02-architecture.md) | BSP 分层、初始化顺序、设备注册、Kconfig/SCons |
| [03-hardware.md](03-hardware.md) | D21x/D213 硬件、开发板资源、地址空间、支持矩阵 |
| [04-boot-kernel.md](04-boot-kernel.md) | 从 BROM 到 `main()`、M-mode、PLIC/CLINT、FPU、内存布局 |
| [05-clock-pinmux-gpio.md](05-clock-pinmux-gpio.md) | 时钟门控、复位、Pinmux 与 GPIO 基础设施 |
| [06-interrupt-timer-dma-cache.md](06-interrupt-timer-dma-cache.md) | 中断分发、Tick、延时、DMA/Cache 现状与约束 |
| [07-uart.md](07-uart.md) | 多 UART 实例、串口配置、收发与调试 |
| [08-i2c.md](08-i2c.md) | I2C 主机、总线扫描、时序与异常处理 |
| [09-can.md](09-can.md) | CAN 2.0A/B 专项：帧格式、过滤、错误状态、测试 |
| [10-pwm-wdt-rtc.md](10-pwm-wdt-rtc.md) | PWM、看门狗、RTC 的使用与限制 |
| [11-gpio.md](11-gpio.md) | GPIO/Pin 设备 API、中断、去抖与板级引脚 |
| [12-storage.md](12-storage.md) | QSPI0/SPI NAND 只读驱动、标准 MTD 接入、分区/FIT 诊断与验证 |
| [13-display-lvgl.md](13-display-lvgl.md) | LVDS/DE Framebuffer、GE Normal、LVGL 9.6.0 显示/触摸端口与限制 |
| [14-connectivity-multimedia.md](14-connectivity-multimedia.md) | USB、SDIO、以太网、Wi-Fi、音频、视频现状 |
| [15-build-pack-deploy.md](15-build-pack-deploy.md) | 工具链、配置、编译、FIT/ITB/IMG 打包、烧录 |
| [16-porting-guide.md](16-porting-guide.md) | 新增 D21x 外设/新板卡的移植指南 |
| [17-testing-troubleshooting.md](17-testing-troubleshooting.md) | 自检、外设测试、常见故障、压力测试建议 |
| [18-decisions.md](18-decisions.md) | 技术决策记录（ADR 风格） |
| [19-maintenance-upstream.md](19-maintenance-upstream.md) | 版本、许可证、HAL 升级、上游贡献注意事项 |
| [20-vendor-source-license.md](20-vendor-source-license.md) | 原厂源码来源、文件清单、SHA-256 与许可证 |
| [21-upstream-readiness.md](21-upstream-readiness.md) | 上游化代码归属、依赖、PR 拆分、许可证与遗留问题 |
| [22-utest-regression.md](22-utest-regression.md) | utest 测试固件、P0/P1/P2 分级、串口脚本和结果判定 |

## 验证状态标记

本项目的所有文档和提交说明统一使用以下六级状态。**“编译通过”不等于“硬件验证通过”。**

| 标记 | 含义 |
|---|---|
| N/I | 尚未实现 |
| SRC | 已编写源码，但未完成编译 |
| BUILD | 当前仓库配置下编译通过 |
| STATIC | 完成静态检查、链接检查或代码审查，但未上板 |
| HW | 在 D213 demo128 实机完成功能验证 |
| STRESS | 完成重复启动、错误注入或压力测试 |

当前总体状态：

- 内核启动、PLIC、CLINT、UART0、调度、IPC、FinSH：**HW**（MSH 输入、tick、显示/触摸等已实机验证）；纯 BSP 的 FPU 上下文为 **KNOWN-FAIL**。
- GPIO、UART1/2/4、PWM：**BUILD + STATIC**，实机验证待做；I2C3、WDT、RTC 已实机验证。
- LVDS/DE Framebuffer、硬件 Color Bar、VSync、GE Normal：**HW**；LVGL 9.6.0 + `lvgl-aic-standalone`：**HW**（软件渲染路径）。
- GT911 触摸：**HW（多点读取）**，方向校准待做。
- CAN 控制器初始化和内部 loopback：**BUILD + STATIC**；外部总线需要第二节点/分析仪，当前为 **SKIP**。
- D213 utest 测试固件：**BUILD + STATIC**，实机 P0/P1 回归待做。
- 存储、MIPI/RGB、USB、SDIO、网络、音频、视频：**N/I**，详见对应文档。

## 最重要的事实

1. 本 BSP 使用标准 RT-Thread v5.3.0 构建体系，不依赖 Luban-Lite 的 `SConstruct`、应用框架或启动脚本。
2. 原厂 Bootloader、SPL、DDR 初始化和 NAND 分区布局保持不变；BSP 只生成被 Bootloader 加载的 OS 镜像。
3. D21x 的 CAN 控制器是 **经典 CAN 2.0A/B**，不支持 CAN FD。仓库中不存在、也不会启用 `hal/canfd` 或 `drv/canfd`。
4. 没有实机串口条件时，任何外设都只能声明为 BUILD/STATIC，不能声明为 HW。
5. `images/`、`build/`、`rtthread.elf` 等生成物不进入版本库；发布镜像必须由使用者用本地 Luban-Lite 资产重新生成。
