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

**决策：** 使用 RT-Thread v5.4.0 标准 BSP 构建、设备模型和 FinSH；只提取必要 HAL。

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

**GE 的本地维护：** `../libraries/soc/d21x/ge/hal_ge_normal.c` 保留事务错误
传播、超时恢复和初始化资源清理。PLIC 中断完成顺序已由公共修复
`5caa519bec2e261146260149e2a8aa1708d79940` 处理，原 GE 重复中断临时
守卫已撤销，见 13.10、13.18 和 20.5。

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

- 仅 TX DMA；仅 UART7 有 RTS/CTS 自动流控；无 RS485 方向控制；
- RX 仍为 1 ms 轮询；
- 高吞吐场景需要扩展；
- 与原厂 UART 功能不完全对等。

**何时重新评估：** 需要 RX DMA、RS485 方向控制或 1.5 Mbps 以上持续吞吐时。

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

**后续：** 存储部分已按 ADR-021 在 `nand_scratch` 上推进（声明分区
仍只读）；显示、USB Host 和以太网已分阶段实现，见
[12-storage.md](12-storage.md)、[13-display-lvgl.md](13-display-lvgl.md)、
[14-connectivity-multimedia.md](14-connectivity-multimedia.md)。

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

**决策：** 只创建 `d213ecv-demo-v4-0`；父目录 `bsp/artinchip/` 保留家族 README。

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

---

## ADR-018：SDMC 使用控制器内部 IDMA，而不是共享 DMAC

**状态：** 已采纳

**背景：** SDMC 自带 4 KiB 描述符的 IDMA，共享 DMAC 也能做内存搬运，但
会把 SD 数据面耦合到外部通道仲裁。

**决策：**

- SD 卡数据走 SDMC 内部 IDMA，CPU FIFO 只用于小传输和失败回退；
- 描述符和 bounce buffer 由 `libraries/soc/d21x/drivers/drv_dma.c` 保证 64 B 对齐；
- 完成判定用 DAT_DONE + 末描述符 OWN，不依赖 SDMAST。

**理由：**

- 内部 DMA 与卡控制器时序绑定，少一层仲裁；
- 不占用共享通道，DMAC 可作为 UART/SPI/I2S 的独立资源。

**代价：**

- 需要维护描述符链和 Cache 所有权；
- 失败时要做控制器软复位。

---

## ADR-019：外部 DMAC 暂用 BSP 自有通道 API，不接入 RT_USING_DMA

**状态：** 已采纳（可随 DM 迁移重新评估）

**背景：** RT-Thread 的 `components/drivers/dma` 依赖 `RT_USING_DM` 和
OFW 来查找控制器/通道，本 BSP 目前是非 DM 的直接注册模型。

**决策：**

- 移植原厂 DMA v1.0 HAL，保留 `hal_request_dma_chan()` 等接口；
- 由 `../libraries/soc/d21x/drivers/drv_dmac.c` 负责时钟、复位和 PLIC IRQ 32；
- 不为了 DMA 单独启用 DM/OFW。

**理由：**

- 避免为一个控制器引入整套设备模型；
- 与原厂 HAL 保持可对照，便于后续接入 UART/SPI/I2S。

**代价：**

- 没有 RT-Thread 标准 DMA 设备节点；
- 如果后续整个 BSP 迁移到 DM，应改为注册 `rt_dma_controller`。

## ADR-020：音频复用原厂 Codec HAL + 标准 RT-Thread audio 设备

**状态：** 已采纳

**背景：** D21x 的音频是独立的 DSPK/DMIC Codec，不是 I2S。原厂提供了
寄存器级 HAL，但原厂 `drv/audio` 依赖 OSAL/PM，并且其 `rt_audio_ops`
带有 `get_avail`/`pause`/`direct_transfer`，与本仓库 RT-Thread 5.4 的
`dev_audio.c` 不一致。

**决策：**

- 逐字节复用 `hal_audio.c`、`hal_audio.h`、`hal_audio_reg.h`；
- 在 `../libraries/soc/d21x/drivers/drv_audio.c` 里针对 RT-Thread 5.4 的 `rt_audio_ops`
  实现 init/getcaps/configure/start/stop/transmit/buffer_info，注册
  `sound0`；
- PLL_FRA1 分数分频与 CLK_CODEC 分频放在
  `../libraries/soc/d21x/port/aic_hal_clk.c`，不引入原厂完整 CMU；
- 功放 PA_SHDN 使用标准 `rt_pin` API，播放时打开、关闭设备时关断。

**理由：**

- 复用原厂寄存器序列，减少调试风险；
- 不引入 OSAL/PM/完整时钟框架；
- 应用可以直接使用 RT-Thread audio API。

**代价：**

- 实现播放和 DMIC 采集；AMIC 采集、I2S、PM 待后续；
- DMA 周期回调运行在 DMAC 中断上下文，经 `rt_audio_tx_complete()`
  进入框架的 replay 队列，路径必须是 ISR 安全的；
- 音频不参与中断优先级分级，与其它中断同为优先级 1。

---

## ADR-021：存储栈采用“只读 FAT + scratch LittleFS + 双槽 BBT”

**状态：** 已采纳

**背景：** `rodata` 是原厂写入的 FAT 镜像；`nand_scratch` 是未被
`image_cfg.json` 声明的尾部，可以安全写入。RT-Thread DFS v1 需要挂载
点和块设备层，而 LittleFS 本身不处理 NAND 坏块。

**决策：**

- `rodata` 通过只读块设备 `nand_rodata` 交给 stock elm-chan FAT；
  写路径返回 0 扇区，底层 MTD 也拒绝 Program/Erase；
- `nand_scratch` 上由 BSP 维护双槽持久化 BBT（CRC32、序号、状态数组、
  逻辑到物理映射、默认 4 块备用池），再在其上运行未修改的
  LittleFS v2.5；
- 用最小只读 romfs 挂到 `/`，只提供 `/rodata`、`/nand`、`/dev`
  挂载点；
- 不修改 `image_cfg.json`，不把 `data`/`data_r` 注册为线性 MTD，
  不做 OTA/A-B。

**理由：**

- 声明分区和启动链保持只读，破坏性写入只落在 scratch；
- 坏块处理集中在 BSP 一层，LittleFS 保持上游原样；
- FAT 镜像是原厂数据格式，不需要重新打包 `rodata`。

**代价：**

- 文件系统只有 102 个逻辑块（102 × 128 KiB ≈ 12.75 MiB）；
- BBT 没有掉电注入和 1000 次循环验证，备用池只有 4 块；
- NFTL、Refresh、OTA/A-B 更新引擎仍需单独设计（只读诊断见 ADR-022）。

---

## ADR-022：OTA 边界——BSP 只提供引导契约与只读诊断，不内置 OTA 引擎

**状态：** 已采纳

**背景：** D213 的 A/B 选择由原厂 Bootloader 完成：它读取冗余
`env`/`env_r`（CRC32 + FLAG_INCREMENTAL），使用 `osAB_next`/
`osAB_now` 选择 `os` 或 `os_r`；OTA 下载、包解析、签名校验、版本
比较和回滚属于上层。仓库内多数 BSP 也只暴露分区/FAL/启动契约，
不把完整 OTA 状态机放进 BSP。

**决策：**

- BSP 只提供引导契约的**只读**诊断：
  `board/ota_slot.c` 解析 `env`/`env_r` 的 CRC、flags 和
  `key=value`，复现 Bootloader 的 FLAG_INCREMENTAL 选择规则；
  `aic_nand_get_fit_info()` 读取 `os`/`os_r` 的 FIT 头；
  `aic_ota_slot_status` 输出活动副本、A/B 键和两个槽的 FIT 状态；
- BSP 不写 `env`、不切换 `osAB_next`、不写 `os`/`os_r`，也不实现
  下载传输、包格式、签名/回滚策略；
- 这些能力由 `packages/` 或产品应用实现，产品必须自己保证写
  `env`/`os_r` 时的原子性、坏块语义和失败恢复；
- 烧录工具默认 `flash scope=os`，只替换运行槽，保留
  `env`/`env_r`/`os_r`；任何全量烧录都需要显式确认。

**理由：**

- 保持 BSP 与 Bootloader 的边界清晰，不复制一套 OTA 状态机；
- 只读诊断能验证启动契约和分区映射，又不会在 Bring-up 阶段误写
  启动链；
- 上层 OTA 可以独立测试和替换，BSP 不引入闭源依赖。

**代价：**

- 当前没有可用的 A/B 升级、回滚和签名校验；
- 写入侧（`env` 提交、`os_r` 写入、坏块跳过）仍需产品/OTA 组件
  设计并做掉电注入；
- 只读诊断不能替代 Bootloader 自身的坏块映射验证。

---

## ADR-023：USB0 Device 复用 CherryUSB AIC UDC 端口，BSP 只做 CDC ACM 胶水

**状态：** 已采纳

**背景：** D21x 的 USB0 既有 EHCI/OHCI 主机控制器，也有独立的
设备控制器（UDC，基址 `0x10200000`、中断 34）。CherryUSB 公共组件
自带 `port/aic/usb_dc_aic.c` 和 `usb_dc_aic_ll.c`，原厂
`port/rtthread/rt_usbd_serial.c` 又能把 CDC ACM 注册成 RT-Thread
字符设备；但该组件从未在 AIC 上编译，端口与当前 1.6.1 核心的
API 不一致。

**决策：**

- 由 BSP 的 `BSP_USING_USB0_DEVICE` 打开
  `RT_CHERRYUSB_DEVICE`、CDC ACM 类和 chardev，选择 UDC IP
  `aic` 和高速速率；该选项与 `BSP_USING_USB0_HOST` 互斥；
- `board/usb_device.c` 只负责描述符（VID `0x33C3`、
  PID `0x6688`）、`usbd_cdc_acm_serial_init()` 和
  `usbd_initialize()`，并暴露 `usb_dev_status`、
  `usb_dev_console on|off`；
- 低层寄存器、时钟/复位和中断仍留在原厂 AIC 端口；
- BSP 在 `aic_compat.h` 提供 `USB_DEV_BASE`、`USB_DEV_IRQn`、
  `AIC_USING_USB0_DEVICE` 和 `csi_dcache_clean_range()` 映射，
  在 `aic_hal_clk.c`/`aic_hal_reset.c` 增加 `CLK_USBD`、
  `RESET_USBD`；
- 对组件 `port/aic` 做最小通用修复：补齐 `busid` 参数、
  统一 `USBD_IRQHandler(uint8_t)` 签名并用 ISR 包装注册、
  包含 `aic_core.h`。这些是端口与同组件核心的接口错误，不涉及
  核心或类驱动，任何启用 AIC 设备端口的 BSP 都会遇到；
- 不修改 `usbd_core`、CDC 类或 RT-Thread 设备框架。

**理由：**

- 复用已有 UDC 端口和 RT-Thread chardev 适配，避免复制 1900 行
  原厂代码到 BSP；
- USB0 与 BROM/aicupg 共用 PHY，Kconfig 互斥能避免同一镜像里
  主机/设备角色冲突；
- CDC ACM 是无驱动依赖的标准类，适合做第二控制台和 Bring-up
  数据通道。

**代价：**

- 需要维护一份对组件端口的通用修复，未来同步 CherryUSB 时要
  确认上游是否已修正；
- 当前只验证到构建/静态检查，枚举、CDC 收发和拔插恢复仍需实机；
- OTG 自动角色切换未实现，USB0 的 Host/Device 选择是编译期固定。

---

## ADR-024：加密引擎通过 RT-Thread hwcrypto 暴露，复用原厂 CE HAL

**状态：** 已采纳

**背景：** D21x 的 Crypto Engine 位于 `0x10020000`，中断 33，
支持 AES/SM4/DES/3DES、MD5/SHA-1/SHA-2/SM3、RSA/SM2 和 TRNG。
原厂 `bsp/artinchip/hal/ce/hal_ce.c` 提供任务描述符和轮询接口，
`bsp/artinchip/drv/ce/drv_ce.c` 已经按 RT-Thread hwcrypto 的
`create/destroy/copy/reset` 设备模型实现 AES 与哈希，但该驱动尚未
被本 BSP 编译过。产品若直接使用原厂私有 crypto 设备，会绕过标准
设备模型并重复实现缓存/对齐处理。

**决策：**

- 新增 `BSP_USING_CE`，选择 `RT_USING_HWCRYPTO` 及 AES
  ECB/CBC、MD5、SHA-1、SHA-2 系列；设备注册为框架默认名
  `hwcryto`，供 `rt_hwcrypto_dev_default()` 使用；
- `../libraries/soc/d21x/hal/hal_ce.c`、`../libraries/soc/d21x/include/hal_ce.h` 从原厂逐字节
  复制并保留版权；`../libraries/soc/d21x/include/aic_utils.h` 是兼容薄包装；
- `../libraries/soc/d21x/drivers/drv_ce.c` 从原厂驱动适配：保留 AES/哈希/RSA 逻辑，
  去掉 DCE CRC 分支，修正 bignum exptmod 目标缓冲区判断中把
  `==` 写成 `=` 的缺陷；
- 时钟通过现有 `aic_hal_clk.c` 的 `CLK_CE` 分支使能/复位
  （`0x0418`，bit 12/bit 8），不引入完整 CMU 框架；
- CE 使用轮询，不注册中断；每次任务前后用
  `aicos_dcache_clean_range()` / `aicos_dcache_invalid_range()`
  维护 C906 D-Cache，输入/输出/密钥/IV 不在 cache line 边界时由
  驱动分配对齐 bounce buffer；
- `aic_compat.h` 只补充 `aicos_malloc_align()`、
  `aicos_free_align()`、`rounddown()` 和 `CE_BASE`，不修改
  hwcrypto 框架或内核。

**理由：**

- 标准 hwcrypto 接口与其它 RT-Thread BSP 一致，上层算法代码无需
  感知 D21x 寄存器；
- 原厂 HAL 已覆盖任务描述符、超时和错误位，复用比重写风险低；
- 已知答案向量（FIPS-197、NIST SP 800-38A、RFC 3174/1321/6234）
  可以在无外部仪器的情况下完成实机回归；
- 轮询实现不占用 PLIC 中断资源，也不改变现有中断优先级。

**代价：**

- 2026-11-09 已在 `d213-ce-utest` 上实机回归：`bsp.d213.ce` 5/5
  单元 PASS（HIL `20261109-034918-80696`），包括 SHA-256 分段更新、
  AES-128/256 ECB、AES-128 CBC、64 B 对齐直通 DMA 和 `CLK_CE`
  使能检查；仍未覆盖性能、并发和长时间运行；
- 未暴露 DES/3DES、SM2/SM4、HMAC、TRNG 和安全密钥源，也未启用
  `RT_HWCRYPTO_USING_BIGNUM`，RSA 代码仅保留；
- 驱动用全局 `ref_count` 和互斥锁串行化所有算法，长时间高负载
  下的性能与功耗未测；没有 PM 集成；
- 原厂驱动的 `drv_sha_update()` 不检查内部错误返回值，实机回归
  时需要结合 `hal_crypto_get_err()` 结果判断；本次已知答案向量
  没有触发错误路径。

---

## ADR-025：LittleFS BBT 首次创建写满两个槽，测试命令保护最后副本

**状态：** 已采纳

**背景：** 2026-11-09 在 `d213_storage_soak.py` 首次实机执行时，
擦除活动 BBT 槽并复位后，驱动打印 `blank scratch, created BBT`，
没有从另一槽恢复。根因是 `bbt_build_fresh()` 在
`active_slot < 0` 时调用一次 `bbt_persist()`，而该函数在无活动槽
时只写 slot 0；slot 1 保持空白。于是新建 BBT 实际上只有一份有效
副本，破坏性测试擦掉它后两槽都无效。若原 BBT 的物理映射不是恒等，
重建会丢失 LittleFS 数据，而不仅仅是测试失败。

**决策：**

- `bbt_build_fresh()` 在第一次持久化成功后立即再持久化一次，使
  slot 0/slot 1 分别持有 seq 1/seq 2 的有效记录；
- `nand_lfs_bbt_test confirm` 在擦除前读取另一槽；另一槽没有有效
  记录时拒绝执行，避免把最后一份 BBT 擦掉；
- 保持现有恢复语义：从旧槽恢复后不自动重写丢失的槽，下一次真正的
  BBT 更新（坏块替换等）会重新写满两槽。

**理由：**

- 首次创建即满足“双槽冗余”的设计承诺，代价只是多一次擦/写；
- 测试命令只应删除可恢复的副本，保护最后副本可以把故意破坏限制在
  `nand_scratch` 且保证可恢复；
- 不改变 BBT 记录格式、CRC、序列号比较或 LittleFS 数据布局。

**验证：**

- 2026-11-09 实机：修复后首次创建显示 `BBT: seq 2, slots 0/1,
  active 1`；`d213_storage_soak.py --stress-cycles 2` PASS（报告
  `bsp/artinchip/artifacts/d213-storage-soak/20261109-035532`），
  擦除 slot 1 后从 slot 0 恢复为 seq 1/active 0，两轮看门狗复位后
  `/nand` 均重新挂载且 stress 载荷校验通过。

**代价：**

- 从单副本恢复后到下一次 BBT 更新之间仍只有一个有效槽；如果这段
  窗口内再次损坏活动槽，只能重建 BBT。当前没有自动修复副本；
- 未做 1000 次掉电或坏块注入，只验证了一次槽切换和两次写入中复位。

---

## ADR-026：板级工程按实体开发板命名，保留原厂软件配置名

**状态：** 已采纳（2026-11-13）

**背景：** 原目录 `d213-demo128-nand` 沿用 Luban-Lite 的软件配置名，
与 RT-Thread 主线 BSP 以实体板卡命名的惯例不一致；同时存在 SoC 系列
（D21x）、SoC 型号（D213ECV）、开发板（D213ECV-DEMO-V4-0）和原厂配置
（demo128-nand）四个容易混淆的概念。

**决策：**

- BSP 目录改为 `bsp/artinchip/d213ecv-demo-v4-0/`，对应实体开发板
  ArtInChip D213ECV-DEMO-V4-0；
- 文档统一使用 D213ECV-DEMO-V4-0 表示开发板，D213ECV 表示 SoC 型号，
  D21x 表示系列；
- 以下字符串保留不变，因为它们分别属于原厂软件配置、镜像产品名、
  已发布的固件/工具接口或历史证据：
  - `target/d21x/demo128-nand/...`、`pack/image_cfg.json` 的
    `product: d213_demo128` 以及 `d213_demo128_page_2k_block_128k`
    镜像名；
  - Kconfig 符号 `BOARD_ARTINCHIP_D213_DEMO128`，以及全部
    `configs/d213-*.config` 的配置文件名；
  - 固件启动横幅 `ArtInChip D213 demo128 RT-Thread BSP`；
  - HIL/串口工具的 `--confirm-board d213-demo128` 安全确认令牌；
  - USB CDC 序列号描述符 `d213-demo128`；
  - 历史 HIL 报告、Build ID 和 `artifacts/` 中的旧名称。

**理由：**

- 板级目录只表达“支持哪块实体板卡”，不再混入 SDK 配置；
- 原厂配置名和镜像产品名是打包协议的一部分，改动会破坏与 Bootloader、
  升级工具的兼容性；
- Kconfig 符号和 `--confirm-board` 是已工作的内部/操作接口，机械改名
  没有功能收益，反而增加回归面；
- 历史报告必须保持原样才能审计。

**代价：**

- 仓库中仍会出现 `demo128`，维护者需要按本文区分其语义；
- 如未来上游要求彻底移除 `DEMO128` 符号，应在独立变更中同步改
  Kconfig、所有 configs 和生成后的 `rtconfig.h`，不能只改字符串。
