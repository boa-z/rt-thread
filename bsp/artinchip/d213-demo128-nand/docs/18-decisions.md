# 18 技术决策记录（ADR）

本文记录本 BSP 的关键选择，帮助未来维护者理解“为什么这样实现”，而不是只看到代码位置。

---

## ADR-001：保留原厂 Bootloader，不重写启动链

**状态：** 已采纳

**背景：** D21x 的 DDR 初始化、SPI NAND、分区表、FIT 校验和 USB/SD 升级由 ArtInChip Bootloader 完成。

**决策：** BSP 只生成被 Bootloader 加载的 OS 镜像，保留 SPL/PBP/DDR/NAND 布局。

**理由：**

- DDR 参数与板卡强相关，重写风险极高；
- 原厂升级/恢复路径成熟；
- 避免破坏 NAND 分区；
- 让 BSP 聚焦 RT-Thread 侧。

**代价：**

- 必须依赖本地 Luban-Lite 资产打包；
- 不能完全脱离原厂启动链；
- 调试启动问题需要同时理解 Bootloader 日志。

---

## ADR-002：使用标准 RT-Thread，不引入 Luban-Lite 应用框架

**状态：** 已采纳

**背景：** Luban-Lite 提供完整 SConstruct、应用框架、OSAL 和多内核支持。

**决策：** 使用 RT-Thread v5.3.0 标准 BSP 构建、设备模型和 FinSH；只提取必要 HAL。

**理由：**

- 保持 BSP 可独立构建；
- 避免与 RT-Thread 调度/中断冲突；
- 降低依赖和许可证复杂度；
- 便于向 RT-Thread 上游贡献。

**代价：**

- 需要自己写适配层；
- 原厂驱动的 OSAL/DMA/PM 依赖需要裁剪；
- 不能直接复用原厂应用示例。

---

## ADR-003：原厂 HAL 文件逐字节保留，另建兼容层

**状态：** 已采纳

**背景：** 原厂 HAL 依赖 `aic_core.h`、`aic_osal.h`、`aic_drv.h`、CMU 框架等。

**决策：** 复制实际使用的 HAL `.c`/`.h`，保持字节不变；新增 `aic_compat.h` 和 `aic_hal_clk.c` 提供最小运行环境。

**理由：**

- 升级 HAL 时可比对原始文件；
- 许可证/来源清晰；
- 避免把 OSAL 引入 BSP；
- 兼容层小，易审查。

**代价：**

- 兼容层必须覆盖 HAL 用到的所有原语；
- HAL 升级可能新增依赖；
- 不能直接使用原厂 `aicos_*` API。

---

## ADR-004：GPIO/I2C/CAN/PWM/WDT/RTC 采用“HAL + 适配层”

**状态：** 已采纳

**背景：** 这些控制器有一定复杂度，原厂 HAL 已经实现寄存器时序、错误处理、过滤器等。

**决策：**

- 保留原厂 HAL；
- 在 `drivers/` 写标准 RT-Thread 适配；
- 不复制原厂 `drv/*`，因为它们依赖 PM/OSAL/FinSH/getopt。

**理由：**

- 复用成熟控制器逻辑；
- 适配层可以独立测试；
- 避免原厂驱动的额外依赖。

**代价：**

- 需要自己处理 RT-Thread 版本差异（如 CAN ops 的 `sendmsg` 返回类型）；
- 原厂驱动的某些功能（PM、特殊测试命令）不会自动获得。

---

## ADR-005：UART 使用 BSP 自有实现

**状态：** 已采纳

**背景：** 原厂 UART 驱动支持 DMA、RS485、流控、PM，但依赖较多。

**决策：** 保留一个小的 serial v1 驱动，使用 poll 发送 + RX FIFO 中断。

**理由：**

- UART 控制器简单；
- 现有实现已在实机验证；
- 避免 DMA/RS485/PM 依赖；
- 代码量小，易维护。

**代价：**

- 无 DMA、流控、RS485；
- 高吞吐场景需要扩展；
- 与原厂 UART 功能不完全对等。

**何时重新评估：** 需要 1.5 Mbps 以上持续吞吐、RS485 方向控制或 DMA 时。

---

## ADR-006：不实现 CAN FD

**状态：** 已采纳（硬性约束）

**背景：** Luban-Lite 中存在 `hal/canfd`、`drv/canfd`。

**决策：** 只实现 D21x 原生 CAN 2.0A/B；不编译、不暴露 CAN FD。

**理由：**

- D21x CAN 控制器不支持 CAN FD；
- SDK 目录存在不代表 D21x 可用；
- 虚假支持会误导应用和测试。

**代价：**

- 不能使用 64 字节帧/BRS；
- 应用层必须按经典 CAN 设计。

---

## ADR-007：不在本轮硬上存储/显示/USB/网络

**状态：** 已采纳

**背景：** 这些子系统依赖 DMA、Cache、PM、面板时序、PHY、协议栈，且当前没有实机串口/显示条件。

**决策：** 明确标记 N/I，不复制不完整驱动，不伪造测试结果。

**理由：**

- 存储错误可能破坏 Bootloader/数据；
- 显示时序错误可能损坏面板；
- 没有硬件验证的“支持”没有意义；
- 先把基础外设做扎实。

**代价：**

- 当前 BSP 不能直接做图形/联网产品；
- 需要后续分阶段实现。

**后续：** 见 [12-storage.md](12-storage.md)、[13-display-lvgl.md](13-display-lvgl.md)、[14-connectivity-multimedia.md](14-connectivity-multimedia.md)。

---

## ADR-008：`hal_clk_set_freq()` 为 no-op

**状态：** 已采纳

**背景：** 原厂 `hal_pwm_init()` 调用 `hal_clk_set_freq(CLK_PWM, 48000000)`；本 BSP 的 PLL 由 Bootloader 固定。

**决策：** 接受请求但不改硬件，返回 0；频率由 `aic_hal_clk.c` 的固定表保证。

**理由：**

- 避免在 RT-Thread 中重新编程 PLL；
- 避免与 Bootloader 时钟树冲突；
- 让 HAL 初始化继续。

**代价：**

- 动态变频不生效；
- 修改 PLL 的产品必须实现真正 CMU 子集。

**风险控制：** 在文档中明确说明，不把 no-op 当成通用能力。

---

## ADR-009：GPIO 中断按组分发

**状态：** 已采纳

**背景：** D21x 的 GPIO 中断是每组一个 IRQ（68-75），不是每引脚一个。

**决策：** 驱动维护 `[group][pin]` handler 表，组 ISR 读取 status & enable，逐位清 W1C 后回调。

**理由：**

- 匹配硬件；
- 不需要为每个引脚安装 PLIC ISR；
- 支持任意引脚组合。

**代价：**

- 同组高频中断会互相影响；
- 电平触发需要软件保证清除；
- handler 在 ISR 上下文。

---

## ADR-010：I2C 采用阻塞 HAL

**状态：** 已采纳

**背景：** 原厂 HAL 提供阻塞 master send/receive，也提供中断路径。

**决策：** 只用阻塞路径，不注册 I2C 中断。

**理由：**

- 实现简单；
- 对触摸/EEPROM/传感器足够；
- 避免 I2C 中断状态机；
- 当前没有高吞吐 I2C 需求。

**代价：**

- 传输期间占用线程；
- 长传输可能影响实时性；
- 不适合高频 IMU/大块传输。

**何时重新评估：** 需要多设备高采样率或 DMA 时。

---

## ADR-011：文档与代码同级交付

**状态：** 已采纳

**背景：** 原 BSP 只有一份 README，无法支撑长期维护。

**决策：** 建立 `docs/` 体系，覆盖总览、架构、硬件、启动、外设、存储、显示、连接、构建、移植、测试、决策、维护、许可证。

**理由：**

- 维护者需要知道“为什么”；
- 状态必须可追溯；
- 避免把未验证功能写成支持。

**代价：**

- 每次代码变更都要更新文档；
- 文档可能与代码不同步，需要 review。

---

## ADR-012：不创建空 D133/D125 工程

**状态：** 已采纳

**背景：** 目标 SoC 是 D21x；目录结构需要为未来板卡留位置。

**决策：** 只创建 `d213-demo128-nand`；父目录 `bsp/artinchip/` 保留家族 README。

**理由：**

- 没有硬件/资料的工程没有维护价值；
- 避免“目录存在=支持”的误导；
- 未来新增 `d133-<board>/`、`d125-<board>/` 即可。

**代价：**

- 当前只有一个板卡工程；
- 共用代码暂时没有独立库。

---

## ADR-013：显示采用“原厂 DE HAL + 自有 LVDS/FB 适配”

**状态：** 已采纳

**背景：** Luban-Lite 的完整显示框架依赖 platform_driver、PM、DTS、panel 框架和大量可选接口。

**决策：**

- 复用 `aic_hal_de.c` 的 DE 寄存器编程；
- LVDS 只实现 demo128 需要的 single-link 寄存器序列；
- Framebuffer、VSync、RT-Thread Graphic Device 由 BSP 实现；
- 不复制原厂 `drv_fb.c`/`drv_de_core.c` 的完整平台框架。

**理由：**

- 当前需求是单 UI 层、全屏、双缓冲；
- 原厂完整框架依赖过多；
- 自有驱动代码量小，便于审查和调试。

**代价：**

- 不支持视频层、多矩形窗口、CCM/Gamma/PQ 等高级功能；
- 未来需要多图层/多屏时要重新评估。

---

## ADR-014：Framebuffer 固定保留在 DDR 顶部

**状态：** 已采纳

**背景：** GE/DE 需要物理连续、地址稳定的 Framebuffer；RT-Thread 堆分配不能保证长期地址和大小。

**决策：**

- 用 `AIC_FB_RESERVED_SIZE` 在链接脚本中保留区域；
- `__heap_end = __fb_start`；
- 最小配置 `AIC_FB_RESERVED_SIZE=0`，布局不变。

**理由：**

- 地址稳定、物理连续；
- 不与 Bootloader/堆冲突；
- 链接期即可发现越界。

**代价：**

- 显示配置下堆减少 32 MB；
- 修改分辨率/FB 数量时要重新评估保留区。

---

## ADR-015：GE 只使用 Normal 模式，不启用 CMD Queue

**状态：** 已采纳（硬性约束）

**背景：** D21x 的 GE CMD Queue 需要额外的命令缓冲、DMA 和同步设计。

**决策：**

- 只编译 `hal_ge_normal.c` + `hal_ge_hw.c`；
- 每次操作同步启动、等待完成中断；
- 适配层用互斥量串行化。

**理由：**

- 满足 Fill/BitBlt/Scale/Blend/Rotate；
- 避免 CMD Queue 的生命周期和缓存复杂度；
- 与 LVGL 的同步 flush 匹配。

**代价：**

- 没有命令批处理；
- 每次 GE 操作有中断等待开销；
- 未来高性能场景可单独增加 CMD Queue 支持。

---

## ADR-016：LVGL 源码作为外部依赖

**状态：** 已采纳

**背景：** LVGL 是 MIT 第三方项目，完整源码较大；RT-Thread 生态通常通过包管理器引入。

**决策：**

- 固定使用官方 LVGL v9.6.0（commit `80ca777e...`）；
- BSP 只保留 `lvgl/SConscript` 集成入口；
- LVGL runtime、display/input port、GE backend 和 demo 位于外部
  `lvgl-aic-standalone`；
- 通过 `LVGL_DIR` 提供官方源码，通过 `LVGL_AIC_DIR` 提供适配组件；
- 找不到源码或版本不是 9.6.x 时明确报错，不伪造构建成功。

**理由：**

- 避免 BSP 仓库被完整 GUI 源码淹没；
- 便于升级 LVGL；
- 许可证边界清晰。

**代价：**

- 默认构建不包含 LVGL；
- 使用者必须先准备 v9 源码；
- 文档必须明确版本和路径。

---

## ADR-017：显示/GE 使用 C906 Cache range 维护

**状态：** 已采纳

**背景：** DE/GE 是 DMA 主设备，CPU 写 Framebuffer 后必须 clean，GE 写后 CPU 读必须 invalidate。

**决策：**

- 在 `board/cache.c` 实现 C906 `dcache.cpa/ipa/cipa` 范围操作；
- 只在 CPU/控制器所有权切换边界调用；
- 不全局关闭 D-Cache。

**理由：**

- 保持性能；
- 避免“关 Cache 掩盖问题”；
- Framebuffer 和 GE 测试缓冲区均按 64 字节对齐。

**代价：**

- 需要维护 Cache 行对齐；
- 多核/其他 DMA 设备仍需单独处理。
