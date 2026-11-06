# 12 存储系统：QSPI0、SPI NAND 与 MTD 只读接入

## 12.1 当前状态

**只读路径已实现：BUILD + STATIC + HOST PASS；QSPI0/NAND 核心读取已通过实机验证（HW PASS，部分验收项 PENDING）。**

实机已确认：`qspi0` 初始化、JEDEC ID `EF AA 21`（W25N01GV）、
`2048+64 B / 64 pages/block / 1024 blocks`、`STATUS=0x00`、`CFG=0x19`、
物理块 0 为好块、`os` 页 0 连续 100 次读取一致、8 个线性 MTD 分区注册、
写/擦/移动/标坏全部返回 `-RT_EPERM`。
`aic_nand_partitions` 的坏块统计/FIT 输出、冷/热重启和完整外设回归仍为
PENDING。

本轮完成：

- QSPI0 控制器初始化、PB.0~PB.5 pinmux、时钟/复位、软件片选、
  同步 FIFO 传输和超时/错误处理；
- SPI NAND 识别（JEDEC ID）、几何参数匹配、状态寄存器读取、
  Page Read / Read From Cache / OOB 读取、片内 ECC 状态解码、
  坏块标记只读检查；
- 标准 RT-Thread MTD NAND 设备注册（`read_id` / `read_page` /
  `check_block`），其余操作明确拒绝；
- `pack/image_cfg.json` 分区布局的构建期解析与只读 MTD 注册；
- `os` 分区 FIT 头识别；
- 只读 MSH 命令和 utest / HOST 回归。

本轮**没有**实现，也不声称支持：

- NAND Program / Erase / Mark Bad / Refresh；
- NFTL、FatFs、LittleFS、UFFS、YAFFS、OTA；
- DMA、Quad 数据阶段、QSPI 中断/异步模式；
- Bootloader 坏块跳过映射的逆向或持久化 BBT。

因此：

- `data` / `data_r` **没有**注册成线性 MTD 分区（原厂把它们定义为 NFTL）；
- 分区读取是**线性视图**，不跳过坏块；
- 编译通过不等于硬件验证通过。

## 12.2 硬件资源

D213EC-DEMO-V4-0（demo128-nand）：

| 项目 | 参数 |
|---|---|
| NAND | 128 MiB SPI NAND（实际型号以 JEDEC ID 为准） |
| 控制器 | QSPI0，基址 `0x10400000`，IRQ 44 |
| 引脚 | PB.0=HOLD, PB.1=WP, PB.2=CS, PB.3=MISO, PB.4=MOSI, PB.5=CLK，功能 3 |
| Page | 2 KiB |
| Erase Block | 128 KiB（64 Page） |
| OOB | 64 B |
| 控制器输入时钟 | 100 MHz（PLL_FRA0，Bootloader 已配置） |
| 默认总线频率 | 请求 24 MHz，实际分频值由 CCFG 计算并在启动日志打印 |
| 启动链 | BROM → SPL → Bootloader → RT-Thread |

PB.0~PB.11 与 SDMC0（功能 2）互斥。当前 BSP 不启用 SDMC0。

## 12.3 移植来源与许可证

| 来源（Luban-Lite，Apache-2.0） | 用途 |
|---|---|
| `bsp/artinchip/hal/qspi/qspi_hw_v1.0.h` | QSPI 寄存器偏移与位定义 |
| `bsp/artinchip/hal/qspi/hal_qspi_v1x.c` | 初始化、FIFO、传输计数、超时策略 |
| `bsp/artinchip/hal/qspi/qspi_internal.c` | 分频选择、超时计算 |
| `bsp/artinchip/drv/qspi/drv_qspi.c` | 命令/地址/Dummy 拆分与片选时序 |
| `bsp/peripheral/spinand/spinand.c` | NAND 命令序列、ECC 状态、坏块读取 |
| `bsp/peripheral/spinand/winbond.c` | W25N01GV 等型号参数与 ECC 解码 |
| `bsp/peripheral/spinand/inc/spinand.h` | 命令字、寄存器、状态位 |
| `bsp/artinchip/drv/spinand/spinand_port.c` | `spi_nand_cmd_cfg` 到 QSPI 消息的映射 |
| `target/d21x/demo128-nand/pinmux.c` | QSPI0 pinmux 功能号 |

本 BSP 的实现文件：

- `drivers/drv_qspi.c` / `drivers/drv_qspi.h`
- `drivers/drv_spinand.c` / `drivers/drv_spinand.h`
- `tools/gen_nand_parts.py`（构建期由 `pack/image_cfg.json` 生成分区头）
- `tools/test_nand_parts.py`（HOST 测试）
- `utest/tc_spinand_ro.c`（utest 只读回归）

原始文件为 Apache-2.0；本 BSP 新增代码使用 Apache-2.0。未引入原厂闭源
NFTL 静态库、UFFS 或 refresh 库。

## 12.4 QSPI0 驱动

### 12.4.1 模型与范围

`drivers/drv_qspi.c` 注册标准 RT-Thread QSPI 总线 `qspi0`，实现
`rt_spi_ops.configure/xfer`。传输在 CPU 轮询模式下完成：

```text
CS assert
  → 命令 + 地址 + Dummy（单线）
  → 数据阶段（当前固定单线；结构上支持 1/2/4 线切换）
CS deassert
```

不使用 DMA、不使用中断、不使用异步完成量。原因：

- DMA 需要 8 字节对齐和 Cache 维护，第一阶段不值得引入；
- 同步路径足以验证 NAND 命令/ECC/坏块；
- 避免与 `RT_USING_SPI_ISR` 的中断安全路径混用。

### 12.4.2 初始化顺序

1. `aic_board_sysclk_init()` 打开 QSPI0 总线/模块门控并释放复位；
2. `aic_board_pinmux_init()` 配置 PB.0~PB.5 为功能 3；
3. `rt_hw_qspi0_init()`（`INIT_DEVICE_EXPORT`，幂等）：
   - 控制器复位（`CFG.CTRL_RST`，轮询自清零）；
   - 主机模式、Mode 0、MSB、软件 CS、低有效、TX delay、DINVD；
   - 关闭所有中断、清 `ISTS`、复位 FIFO、设置水位；
   - 写 CCFG 分频；RX 采样使用与原厂
     `AIC_DEV_QSPI0_DELAY_MODE=2` 一致的**半周期**模式；
4. 注册总线 `qspi0`，打印基址/输入时钟/请求频率/实际频率及 CMU 寄存器。

SPI NAND 驱动在 attach 之前再次调用 `rt_hw_qspi0_init()`，保证总线先于
设备就绪，不依赖链接顺序。

### 12.4.3 传输与错误处理

- `address.size` 兼容两种调用约定：大于 4 按 bit 处理（上游
  `rt_qspi_send*` 使用 24/32），否则按字节处理（原厂 spinand 使用 2/3/1）；
- `dummy_cycles` 按线宽换算为字节后随命令阶段发出；
- FIFO 等待全部使用 `rt_hw_us_delay(1)` 轮询并带超时；
- 传输结束检查 `ISTS.TDONE`，并检查 TX overflow / RX underflow / RX overflow；
- 命令-only 消息返回 1，避免上游 `rt_qspi_transfer_message()` 把 0 当作
  I/O 错误。

> **上游 API 注意**：本 BSP 使用
> `rt_spi_configure(&qspi->parent, &cfg.parent)` 而不是
> `rt_qspi_configure()`。当前 RT-Thread 的 `rt_qspi_configure()` 只更新
> `rt_qspi_device.config`，随后传给总线 `configure` 的
> `device->parent.config` 仍是全 0，实测会让 QSPI 以 LSB、0 Hz 配置启动，
> NAND 命令位序全部反转（JEDEC ID 读回 `FF FF FF FF`）。
> 这与 Nuvoton SPINAND 端口的处理方式一致。

由于写操作在本轮被禁止，QSPI 只发送 NAND 读命令和
GET_FEATURE/SET_FEATURE（配置寄存器）。详细日志见 12.9。

## 12.5 SPI NAND 只读驱动

### 12.5.1 支持芯片

驱动不根据原厂配置假定型号。启动时读取 4 字节 JEDEC ID，并与下表匹配；
未知 ID 直接报错，不猜测几何参数：

| JEDEC ID | 型号 | Page+OOB | Page/Block | Block 数 | 片内 ECC |
|---|---|---|---|---|---|
| `EF AA 21` | W25N01GV | 2048+64 | 64 | 1024 | 1 bit/512B |
| `EF AE 21` | W25N01KV | 2048+64 | 64 | 1024 | 4 bit/512B |
| `EF AA 22` | W25N02KV | 2048+128 | 64 | 2048 | 1 bit/512B |
| `EF BA 22` | W25N02KW | 2048+128 | 64 | 1024 | 1 bit/512B |
| `EF BF 22` | W25N02JW | 2048+64 | 64 | 2048 | 1 bit/512B |

D213 demo128 的 image_cfg 几何为 2 KiB Page / 128 KiB Block / 64 B OOB，
对应 128 MiB 型号。驱动会把识别结果与构建期生成的
`SPINAND_CFG_*` 常量比较；不一致时打印警告并**拒绝注册分区**，但仍保留
原始物理 Page 只读能力用于诊断。

### 12.5.2 初始化

```text
RESET (0xFF) + 等待 BUSY 清零
READ ID (0x9F, 4 bytes)
匹配型号表
GET_FEATURE CFG (0xB0)
SET_FEATURE CFG: 置 BUF_READ + ECC_ENABLE
GET_FEATURE CFG/STATUS 回读并打印
分配 page + oob 内部缓冲
```

与原厂流程的差异：

- 不执行 `spinand_lock_block(0)`：读操作不需要解锁，减少一次配置写；
- 不使能 Quad（`CFG.QUAD_ENABLE`）：第一阶段固定单线数据阶段；
- 不启用 SPIENC、不做 BBT 扫描和持久化。

除上述 CFG 配置位外，驱动不修改任何 NAND 内容。

### 12.5.3 Page 读取与 ECC

```text
PAGE_READ (0x13, 3-byte page)
  → 轮询 STATUS.BUSY，超时 30000 × 1 µs
  → 解码 STATUS[5:4]
       00 = 无位翻转
       01 = 已纠错（数据有效）
       10 = 不可纠错（数据无效）
       11 = 保留/异常（按错误处理）
  → READ FROM CACHE (0x03, 2-byte column, 8 dummy cycles)
       column=0            : 主区，可附加 OOB
       column=page_size    : 纯 OOB
```

返回语义：

| 场景 | 原始 helper | MTD `read_page` |
|---|---|---|
| 正常 | `SPINAND_RO_OK` | `RT_MTD_EOK` |
| ECC 已纠错 | `SPINAND_RO_ECC_OK` | `RT_MTD_EECC_CORRECT` |
| 不可纠错 | `SPINAND_RO_EECC` | `RT_MTD_EECC` |
| 忙超时 | `SPINAND_RO_ETIMEOUT` | `RT_MTD_EBUSY` |
| 传输/参数错误 | `SPINAND_RO_EIO/EINVAL` | `RT_MTD_EIO` |

**不会静默忽略 ECC 错误**：不可纠错时不返回页数据；已纠错时通过
`RT_MTD_EECC_CORRECT` 明确上报。

### 12.5.4 坏块只读检查

Winbond 系列出厂坏块标记位于块首页 OOB 第 0 字节：

- `0xFF`：好块；
- 其他值（原厂标记写入 `0xF0`）：坏块。

`check_block` 读取该字节，不写任何标记。返回约定：

- `RT_EOK`：好块；
- `-RT_ERROR`：检测到坏块；
- `RT_MTD_EIO` / `RT_MTD_EECC`：读取失败，不能据此判定好坏块。

## 12.6 标准 MTD NAND 接入

### 12.6.1 上游接口与兼容层

使用上游 `components/drivers/include/drivers/mtd_nand.h` 的标准 7 个 ops，
不修改公共头文件，也不把原厂 13 槽扩展（`continuous_read`、块状态、
OOB map、bitflip 回调）塞进上游结构体。

| 标准 op | 实现 | 说明 |
|---|---|---|
| `read_id` | `spinand_mtd_read_id` | 重新读取 JEDEC ID |
| `read_page` | `spinand_mtd_read_page` | 绝对页 = `block_start*ppb + 分区相对页` |
| `write_page` | 拒绝桩 | `-RT_EPERM` |
| `move_page` | 拒绝桩 | `-RT_EPERM` |
| `erase_block` | 拒绝桩 | `-RT_EPERM` |
| `check_block` | `spinand_mtd_check_block` | 只读坏块标记 |
| `mark_badblock` | 拒绝桩 | `-RT_EPERM` |

拒绝桩必须是**非空函数**：上游 `mtd_nand.c` 对 `write_page` / `move_page` /
`erase_block` 使用 `RT_ASSERT`，置 `NULL` 会在调用时断言，而不是返回错误。

### 12.6.2 注册标志审查

上游 `rt_mtd_nand_register_device()` 固定以
`RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_STANDALONE` 注册。驱动在注册成功后
把 `device->parent.flag` 改为 `RT_DEVICE_FLAG_RDONLY | STANDALONE`，用于
`list_device()` 和调试时显示真实能力。

**该标志只是自述，不构成保护。** 真正的只读保证来自 ops 桩，以及：

- 注册后把 `device->parent.ops` 换成本 BSP 的只读设备 ops：
  - 通用 `rt_device_read()` 返回 `-RT_ENOSYS`（防止把“返回 length”
    误判为页读取成功）；
  - 通用 `rt_device_write()` 返回 `-RT_EPERM`；
  - 读写 NAND 必须走 `rt_mtd_nand_read()` 等 MTD API；
- 不注册 `rt_blk_nand` 块设备；
- 不挂载任何文件系统；
- MSH 命令不提供任何写/擦/修复入口。

### 12.6.3 地址语义

- MTD 设备的 `block_start` / `block_end` 是**绝对物理块号**；
- `read_page` / `check_block` 的 `page` / `block` 参数是**分区相对**编号；
- 原始 MSH 命令 `aic_nand_read_page` / `aic_nand_check_block` 使用
  **整片物理**编号；
- 分区逻辑字节偏移 = `分区相对页号 × page_size + 页内偏移`；
- 物理字节偏移 = `绝对页号 × page_size + 页内偏移`。

三种编号在 `aic_nand_partitions`、`aic_nand_read_part` 的输出和本文档中
分开描述，避免混用。

## 12.7 分区访问

### 12.7.1 单一数据源

分区表不手写第二份常量。`tools/gen_nand_parts.py` 在构建时读取
`pack/image_cfg.json`（去除注释后按 JSON 解析），完成：

- 名称、顺序、大小、偏移推导；
- 128 KiB 对齐检查；
- 范围不越过介质末尾；
- 顺序不重叠；
- 标记带 `nftl` 的子对象为非线性。

生成结果写入 `build/spinand_parts_gen.h`（`build/` 已被 .gitignore 忽略），
由 `drivers/drv_spinand.c` 使用。即使将来修改分区，固件与打包配置也不会
出现两套偏移。

### 12.7.2 线性分区与 NFTL 分区

| 分区 | 类型 | 是否注册 MTD |
|---|---|---|
| `spl` | 线性 | 是（只读） |
| `env` / `env_r` | 线性 | 是（只读） |
| `os` / `os_r` | 线性 | 是（只读） |
| `rodata` / `rodata_r` | 线性 | 是（只读） |
| `data` / `data_r` | **NFTL** | **否**，仅物理 Page 只读 |
| `refresh` | 线性 | 是（只读） |

`refresh` 只是允许被读取，**不会**启动原厂 refresh 维护；驱动中没有
refresh 回调、没有后台线程、没有写回。

### 12.7.3 坏块与 Bootloader 映射

当前可以确认的只有：

- 分区起止来自 `image_cfg.json`；
- 每个块首页 OOB[0] 的出厂坏块标记；
- `os` 分区首页是否具有 FIT 魔数。

**不能确认** Bootloader/打包工具在写入时是否跳过坏块、以及跳过后的逻辑
地址如何映射。因此本驱动：

- 不伪造“坏块已重映射”；
- 分区读取保持线性（`绝对页 = start_block × ppb + 相对页`）；
- 在 `aic_nand_partitions` 中逐分区统计坏块数并给出警告；
- 如果 `os` 首页不是有效 FIT 头，只报告“未检测到”，不宣称读取成功。

要获得与 Bootloader 完全一致的分区视图，下一步必须取得其坏块映射规则或
分区元数据，并做逐页对比；在此之前，`aic_nand_read_page` 的物理页读取是
唯一不依赖该假设的访问方式。

### 12.7.4 `os` FIT 识别

`aic_nand_partitions` 通过标准 `rt_mtd_nand_read()` 读取 `os` 分区第 0 页
的前 8 字节：

- magic = `0xD00DFEED`；
- totalsize 位于大端偏移 4，要求 `0x100 <= totalsize <= 分区大小`。

满足时打印 `FIT image detected`，否则打印实际 magic/totalsize。该检查
用于证明分区映射和读取链路至少对 `os` 头部成立，而不是用“数据不全为
0xFF”作为成功证据。

## 12.8 配置

### 12.8.1 Kconfig

```text
BSP_USING_QSPI0              select RT_USING_SPI + RT_USING_QSPI
BSP_QSPI0_FREQ               默认 24000000
BSP_QSPI0_INPUT_HZ           Bootloader 已配置的 QSPI0 输入时钟，默认 100000000
BSP_USING_SPINAND_MTD_NAND   depends on BSP_USING_QSPI0
                             select RT_USING_MTD_NAND
BSP_SPINAND_READONLY         default y；关闭时驱动编译报错
```

BSP 不重新配置 SoC 时钟树。`BSP_QSPI0_INPUT_HZ` 用于计算分频；如果实测
`QSPI0_CLK` 与 100 MHz 不同，应先改这个值，而不是盲目调 `BSP_QSPI0_FREQ`。

默认 `.config`、`d213-minimal`、`d213-default` 均**不开启** QSPI/NAND，
原有外设行为不变。

### 12.8.2 可复现配置

`configs/d213-nand-readonly.config` 基于 minimal，额外开启：

```text
CONFIG_RT_USING_SPI=y
CONFIG_RT_USING_QSPI=y
CONFIG_RT_USING_MTD_NAND=y
CONFIG_BSP_USING_QSPI0=y
CONFIG_BSP_QSPI0_FREQ=24000000
CONFIG_BSP_USING_SPINAND_MTD_NAND=y
CONFIG_BSP_SPINAND_READONLY=y
```

使用：

```sh
cd bsp/artinchip/d213-demo128-nand
cp configs/d213-nand-readonly.config .config
scons --pyconfig-silent
scons -j8
```

`d213-utest.config` 也已加入同一组开关，因此标准 utest 固件会包含
`bsp.d213.spinand.ro`。

## 12.9 MSH 命令（全部只读）

| 命令 | 语义 |
|---|---|
| `aic_nand_info` | JEDEC ID、型号、Page/OOB/Block/容量、ECC 强度、QSPI 频率 |
| `aic_nand_status` | 读 `STATUS(0xC0)` 与 `CFG(0xB0)`，解码 BUSY/ECC/配置位 |
| `aic_nand_read_page <physical_page>` | 读取整片物理页，打印 ECC 结果、主区前 32 B、OOB 前 16 B |
| `aic_nand_check_block <physical_block>` | 读取出厂坏块标记，输出 good/FACTORY BAD |
| `aic_nand_partitions` | 分区表、线性/NFTL 类型、坏块计数、`os` FIT 检测 |
| `aic_nand_read_part <name> <offset> <length>` | 从线性分区读取，offset 为分区内字节偏移，length ≤ 4096，输出 CRC32 与前 64 B |

边界规则：

- `read_page`：`page < block_count × pages_per_block`；
- `check_block`：`block < block_count`；
- `read_part`：只接受已注册的线性分区；`offset + length ≤ 分区大小`；
- 所有参数都做完整解析和范围检查，失败打印具体错误。

启动日志示例（硬件实测后应替换为真实输出）：

```text
qspi0: registered, base=0x10400000 sclk=100000000 Hz requested=24000000 Hz actual=16666666 Hz cmu=0x........
spinand: JEDEC ID EF AA 21 FF
spinand: W25N01GV, 2048+64 B, 64 pages/block, 1024 blocks
spinand: CFG=0x18 STATUS=0x00
spinand: 8 linear MTD partition(s) registered, read-only
```

`CFG=0x18` 表示 BUF_READ(bit3) + ECC_ENABLE(bit4) 已置位；
`STATUS=0x00` 表示空闲、无 ECC 事件。

## 12.10 测试与验证

### 12.10.1 已执行（BUILD / STATIC / HOST）

| 项目 | 结果 | 说明 |
|---|---|---|
| `d213-minimal` clean build + ELF/map | PASS | 原有配置未回归 |
| `d213-default` clean build + ELF/map | PASS | 原有外设/显示未回归 |
| `d213-utest` clean build + ELF/map | PASS | 新增 `bsp.d213.spinand.ro` |
| `d213-nand-readonly` clean build + ELF/map | PASS | 新增 QSPI/NAND/MTD |
| `d126-minimal` clean build + ELF/map | PASS | 共享检查脚本路径分隔符修复后通过 |
| `tools/test_nand_parts.py` | 4/4 PASS | 布局、对齐、越界、头文件生成 |
| `tools/gen_nand_parts.py --check` | PASS | 128 MiB / 1024 block / 10 分区 |

构建命令：

```sh
python bsp/artinchip/tools/build-check.py \
    --targets d213-minimal d213-default d213-utest d213-nand-readonly d126-minimal
python bsp/artinchip/d213-demo128-nand/tools/test_nand_parts.py -v
```

### 12.10.2 硬件验收步骤

#### 2026-11-06 实机结果（第一轮，部分 PASS）

烧录 `artifacts/d213-nand-readonly` 固件（W25N01GV 板卡），启动与读取链路
已经实机确认：

- `qspi0: registered, base=0x10400000 sclk=100000000 Hz
  requested=24000000 Hz actual=16666666 Hz cmu=0x00003105`；
- `spinand: pre-reset ID EF AA 21 00`，复位后 `JEDEC ID EF AA 21 00`，
  识别为 W25N01GV，`2048+64 B, 64 pages/block, 1024 blocks`；
- `spinand: CFG=0x19 STATUS=0x00`（Quad/BUFFER/ECC 均开启，非 busy）；
- `spinand: 8 linear MTD partition(s) registered, read-only`；
- `aic_nand_status`：`busy: no`、`ecc bits 0`，QSPI 寄存器
  `TCFG=0x41c0`（MSB + 半周期采样）、`CCFG=0x00001002`；
- `utest_run -thread bsp.d213.spinand.ro` 整个用例 **HW PASS**，
  覆盖 JEDEC 识别、物理块 0 好块、`os` 页 0 连续 100 次读取一致、
  越界拒绝、写/擦/移动/标坏拒绝。

排障记录（本轮修复的两个实机缺陷）：

1. **LSB 位序反转**：上游 `rt_qspi_configure()` 不把 mode/max_hz 传到
   `parent.config`，驱动按全 0 配置启动成 LSB（`TCFG bit12=1`），
   JEDEC ID 读回 `FF FF FF FF`；改为调用
   `rt_spi_configure(&qspi->parent, &cfg.parent)` 后恢复正常。
2. **RX 采样沿**：按频率切 `RXDLY_DIS` 会让 STATUS 读回 `0xFF`、
   BUSY 保持 1；固定为原厂默认半周期采样（`AIC_DEV_QSPI0_DELAY_MODE=2`）。

#### 剩余验收项（仍 PENDING）

1. `aic_nand_read_page` 手工读样页并核对 ECC/OOB 输出；
2. `aic_nand_check_block` 抽查 `spl`/`os` 等多个块，记录坏块分布；
3. `aic_nand_partitions`：坏块计数、`os` FIT 检测结果；
4. `aic_nand_read_part os 0 8`：magic 与 `aic_nand_partitions` 一致；
5. 冷启动、热重启各 ≥ 5 次，NAND 读取不改变启动结果；
6. 确认 UART、tick、CAN、显示在含 NAND 的配置下仍正常；
7. 用逻辑分析仪抽查全程无 Program/Erase/Refresh 命令。

### 12.10.3 结果分级

统一使用：

`BUILD / STATIC / HOST / HW / PENDING / FAIL`

当前结论：**BUILD + STATIC + HOST PASS；核心读取链路 HW PASS
（2026-11-06，W25N01GV）；剩余验收项 PENDING**（见 12.10.2）。

## 12.11 风险与限制

1. **坏块映射未知**：见 12.7.3。线性分区读取只保证“按 image_cfg 偏移读取”，
   不保证与 Bootloader 跳过坏块后的逻辑地址一致。
2. **单线同步**：数据阶段固定 1 线，读取速度低于 Quad；未使用 DMA。
3. **仅 Winbond 子集**：实际板卡若为其他厂商，需按 JEDEC ID 增加型号表和
   ECC 解码；未知 ID 会拒绝注册分区。
4. **无写入能力**：这是本轮目标，不是缺陷；文件系统/OTA 必须等写入、
   坏块管理和掉电验证完成后再做。
5. **未验证电源/时序**：QSPI RX 采样固定为原厂默认的半周期模式，仍需示波器
   或逻辑分析仪在目标板上确认。
6. **CFG 写操作**：初始化会通过 SET_FEATURE 修改易失配置寄存器（BUF/ECC），
   这是读取 ECC 状态所必需；不涉及阵列数据、擦除或坏块标记。

## 12.12 后续实现写入/文件系统的技术阻碍

1. 确认 Bootloader 的坏块跳过/分区映射规则，否则写入会破坏启动链；
2. 实现 Program Load / Program Execute / Block Erase / Mark Bad，并区分
   program/erase fail；
3. 设计坏块表持久化与掉电一致性（RAM 缓存不足以跨重启）；
4. 处理 ECC 阈值与位翻转上报，决定何时迁移数据；
5. 在非启动分区（例如 `data`）做写入/擦除/断电循环，先备份再验证；
6. 引入 LittleFS 或 FatFs 前，先完成块设备层和 1000 次掉电测试；
7. `data`/`data_r` 的 NFTL 兼容性需要原厂库或重新设计，不能直接当线性
   块设备使用；
8. OTA 需要 A/B 分区、签名/CRC、回滚策略，不能只依赖 `os_r` 存在。

## 12.13 安全规则（保持有效）

以下操作在未完成写入验证前继续禁止：

- 擦除/写入 `spl`、`bootloader`、`env`、`os`；
- 写入 Bootloader 地址 `0x47B80000`；
- 修改 `pack/image_cfg.json` 的分区偏移/名称/大小；
- 在 RT-Thread 中对整片 NAND 做擦除；
- 关闭 ECC 或忽略坏块标记；
- 启用自动 BBT 持久化、Refresh 或后台维护；
- 把 `data`/`data_r` 当作线性块设备写入。
