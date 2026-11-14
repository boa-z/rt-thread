# 12 存储系统：QSPI0、SPI NAND、MTD 与文件系统

## 12.1 当前状态

**读路径：BUILD + STATIC + HOST PASS，核心读取 HW PASS。
写路径（Program/Erase/Move/Mark-Bad）：已实现并在未分配尾部
`nand_scratch` 上 HW PASS；`image_cfg.json` 声明的分区默认仍为只读。**

**SD 卡（SDMC1）已接入标准 MMCSD/块设备栈：BUILD + STATIC + PACKAGE
PASS；实机卡识别、`sd0`/`sd0p0` 注册、只读 utest、FAT 挂载和 IDMA
64 扇区回归均 HW PASS。SDIO 主机路径由 SDMC2（PF.0..PF.5）承载
RTL8733BS 模组，见 12.15。**

**文件系统层（第二阶段）：`rodata` 中的 FAT 镜像通过只读块设备
`nand_rodata` 挂载到 `/rodata`；`nand_scratch` 上实现持久化坏块表
（BBT）和 LittleFS，挂载到 `/nand`。两者 BUILD + STATIC + PACKAGE +
HW PASS（HIL `20261109-000938-77752`，全量回归
`20261109-002129-38512`），掉电/复位后 LittleFS 数据与
BBT 映射保持。声明分区、`data`/`data_r`、`os`/`os_r` 仍不会被文件
系统层写入。**

**OTA/A-B 只读诊断（第三阶段基础）：`drv_ota_slot.c` 读取
`env`/`env_r` 的冗余环境、校验 CRC32、复现 Bootloader 的
FLAG_INCREMENTAL 选择规则，并读取 `os`/`os_r` 的 FIT 头；
`aic_nand_partitions` 同时报告两个槽的 FIT 状态。
`bsp.d213.ota` 已在实机 PASS（HIL `20261109-015923-59712`：
`osAB_now=A`、`osAB_next=A`、`os` FIT totalsize 729、`os_r` 为空）。
BSP 不写入 `env`/`os`/`os_r`，不包含下载、签名校验或更新状态机；
OTA 引擎仍归 `packages/` 或应用层（ADR-022）。**

实机已确认：`qspi0` 初始化、JEDEC ID `EF AA 21`（W25N01GV）、
`2048+64 B / 64 pages/block / 1024 blocks`、`STATUS=0x00`、`CFG=0x19`、
物理块 0 为好块、`os` 页 0 连续 100 次读取一致、8 个线性 MTD 分区注册、
声明分区的写/擦/移动/标坏全部返回 `-RT_EPERM`、`nand_scratch`
（916~1023）可写且读写回归 PASS。文件系统挂载后串口可见：
`rom /`、`elm nand_rodata /rodata`、`lfs /nand`；
`nand_lfs_info` 报告 108 个物理块、102 个逻辑块、4 个备用块、
2 个 BBT 块、0 个坏块、BBT 序号 1。
`aic_nand_partitions` 的坏块统计输出和 `aic_ota_slot_status` 的 MSH
文本输出已实机确认；BBT 双槽切换和两次看门狗复位注入也已 PASS
（报告 `20261109-035532`）。冷启动掉电注入仍为 PENDING，不用编译
结果代替实机结论。

本轮完成：

- QSPI0 控制器初始化、PB.0~PB.5 pinmux、时钟/复位、软件片选、
  同步 FIFO 传输和超时/错误处理；
- SPI NAND 识别（JEDEC ID）、几何参数匹配、状态寄存器读取、
  Page Read / Read From Cache / OOB 读取、片内 ECC 状态解码、
  坏块标记检查；
- 标准 RT-Thread MTD NAND 设备注册与全部 7 个 ops：
  `read_id` / `read_page` / `write_page` / `move_page` /
  `erase_block` / `check_block` / `mark_badblock`；
- Program Load / Program Execute / Block Erase 命令序列，
  写/擦失败位检查，首次写入前清除块保护；
- `pack/image_cfg.json` 分区布局的构建期解析与 MTD 注册；
  分区默认只读（`BSP_SPINAND_READONLY=y`）；
- 未分配尾部注册为独立可写设备 `nand_scratch`
  （`BSP_SPINAND_SCRATCH_MTD=y` 时），破坏性验证只作用于它；
- `os`/`os_r` 分区 FIT 头识别；
- `env`/`env_r` 冗余环境只读解析、CRC32 和 FLAG_INCREMENTAL 选择；
- `aic_ota_slot_status` 只读 A/B 状态诊断与 `bsp.d213.ota` utest；
- 读写 utest / HOST 回归；
- 只读块设备 `nand_rodata` 与 `rodata` FAT 镜像的 `/rodata` 挂载；
- 持久化双槽 BBT（CRC32、序号、逻辑到物理映射、备用块替换）；
- 未修改的 LittleFS v2.5 + DFS v1 适配层，挂载到 `/nand`；
- 文件系统 utest 与上电自动挂载（`INIT_FS_EXPORT`）。

本轮**没有**实现，也不声称支持：

- Refresh、后台坏块维护、坏块表在 `nand_scratch` 之外的持久化；
- `data` / `data_r` 上的 NFTL、UFFS、YAFFS 兼容访问；
- OTA / A-B 更新引擎、写入侧切换、签名校验和回滚（本轮只有只读诊断）；
- DMA、Quad 数据阶段、QSPI 中断/异步模式；
- Bootloader 坏块跳过映射的逆向。

因此：

- `data` / `data_r` **没有**注册成线性 MTD 分区（原厂把它们定义为 NFTL）；
- 声明分区访问仍是**线性视图**，不跳过坏块；
- 只有 `nand_scratch` 通过 BBT 做坏块跳过；写路径**没有**在
  `spl`/`env`/`os` 等启动链分区上做过破坏性测试；
- 编译通过不等于硬件验证通过。

## 12.2 硬件资源

D213ECV-DEMO-V4-0（demo128-nand）：

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

- `../libraries/soc/d21x/drivers/drv_qspi.c` / `../libraries/soc/d21x/drivers/drv_qspi.h`
- `../libraries/soc/d21x/drivers/drv_spinand.c` / `../libraries/soc/d21x/drivers/drv_spinand.h`
- `tools/gen_nand_parts.py`（构建期由 `pack/image_cfg.json` 生成分区头）
- `tools/test_nand_parts.py`（HOST 测试）
- `utest/tc_spinand_ro.c`（utest 只读回归）

原始文件为 Apache-2.0；本 BSP 新增代码使用 Apache-2.0。未引入原厂闭源
NFTL 静态库、UFFS 或 refresh 库。

## 12.4 QSPI0 驱动

### 12.4.1 模型与范围

`../libraries/soc/d21x/drivers/drv_qspi.c` 注册标准 RT-Thread QSPI 总线 `qspi0`，实现
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
   - 控制器复位（`CFG.CTRL_RST`，轮询自清零）。
     **轮询超时只警告不终止**：ArtInChip v1.0 HAL 对同一位就是
     `readl_poll_timeout + pr_warn` 后继续
     （`qspi_hw_v1.0.h` "CFG_BIT_CTRL_RST not cleared after ..."），
     实机（2026-11-06 第二轮）也确认该位在这片硅片上经常不自清，
     但控制器功能完全正常（同一启动里 SPL 用同一控制器完成
     183496 B 读并 CRC 校验通过）。本轮曾把它当成致命错误而拒绝
     注册总线，实机复现失败后按 HAL 语义改回警告并继续；
   - 主机模式、Mode 0、MSB、软件 CS、低有效、TX delay、DINVD；
   - 关闭所有中断、清 `ISTS`、复位 FIFO（同样超时警告不终止）、
     设置水位；
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

读路径只发送 NAND 读命令和 GET_FEATURE/SET_FEATURE（配置寄存器）；
写路径额外发送 WRITE_ENABLE / PROGRAM_LOAD / PROGRAM_EXECUTE /
BLOCK_ERASE。详细日志见 12.9。

## 12.5 SPI NAND 驱动

### 12.5.1 支持芯片

驱动不根据原厂配置假定型号。启动时读取 4 字节 JEDEC ID，并与下表匹配；
未知 ID 直接报错，不猜测几何参数。**只有 `hw_verified=1` 的型号允许注册
MTD 分区**；其余型号仅保留原始物理页只读诊断（`aic_nand_*`），
不注册分区、不提供 MTD 语义：

| JEDEC ID | 型号 | Page+OOB | Page/Block | Block 数 | 片内 ECC | 硬件验证 |
|---|---|---|---|---|---|---|
| `EF AA 21` | W25N01GV | 2048+64 | 64 | 1024 | 1 bit/512B | **=1（本 BSP 实机）** |
| `EF AE 21` | W25N01KV | 2048+64 | 64 | 1024 | 4 bit/512B | =0 |
| `EF AA 22` | W25N02KV | 2048+128 | 64 | 2048 | 8 bit/512B | =0 |
| `EF BA 22` | W25N02KW | 2048+128 | 64 | 2048 | 8 bit/512B | =0 |
| `EF BF 22` | W25N02JW | 2048+64 | 64 | 2048 | 1 bit/512B | =0 |

参数来源为 V1.3.3 `bsp/peripheral/spinand/winbond.c`。此版本已将
W25N02KV/KW 的 ECC 修正为 8 bit/512B，并将 KW 的 Block 数修正为 2048。
W25N01KV 的 OOB 大小仍与 Linux 主线存在 64 B / 96 B 的差异。
这些参数修正不等于硬件验证；除历史验证的 W25N01GV 外，其余型号仍只供诊断，
必须按 datasheet 核对并完成芯片级验证后才允许注册分区。

D213 demo128 的 image_cfg 几何为 2 KiB Page / 128 KiB Block / 64 B OOB，
对应 128 MiB 型号。驱动会把识别结果与构建期生成的
`SPINAND_CFG_*` 常量比较；不一致时打印警告并**拒绝注册分区**；未验证
型号（`hw_verified=0`）也直接拒绝注册并打印原因，但仍保留原始物理
Page 只读能力用于诊断。

### 12.5.2 初始化

```text
READ ID (0x9F, 4 bytes)        ← 复位前先读，便于区分链路/复位问题
RESET (0xFF) + 等待 BUSY 清零   ← 最多两次，成功后校验 STATUS 合法
匹配型号表
GET_FEATURE CFG (0xB0)
SET_FEATURE CFG: 置 BUF_READ + ECC_ENABLE
GET_FEATURE CFG/STATUS 回读并打印   ← CFG 未置位判失败
分配 page + oob 内部缓冲
```

与原厂流程的差异：

- 初始化阶段不执行 `spinand_lock_block(0)`：读操作不需要解锁，减少
  一次配置写；第一次 Program/Erase 前才读取 `0xA0`，非 0 时清零并打印；
- 不使能 Quad（`CFG.QUAD_ENABLE`）：固定单线数据阶段；
- 不启用 SPIENC、不做 BBT 扫描和持久化。

**复位严格化（新增）**：一次 RESET 后 `wait_busy` 超时、STATUS 读失败或
读回 `0xFF`（读链路异常的典型值）都算复位未完成，允许整体重试一次；
两次都失败则初始化直接失败（`-RT_ERROR`），**不再静默跳过、也不会注册
看似正常的设备**。重复调用 `rt_hw_spinand_ro_init()` 会被首末状态拒绝，
不会重复初始化互斥锁或重新 attach。

**初始化失败路径**：spinand attach/复位/probe 失败时不会调用分区注册，
即 MTD 全部为未注册状态，`aic_nand_*` 诊断命令仅报告"未初始化"。
QSPI 侧的复位/FIFO 轮询超时**不是**失败路径（原厂 HAL 语义为警告并
继续，见 12.4.2）；`rt_hw_qspi0_init()` 仍保留失败短路逻辑作为防御，
总线注册失败时 NAND 初始化即失败。

初始化阶段除上述 CFG 配置位外不修改任何 NAND 内容；只有后续显式
Program/Erase/Mark-Bad 调用才改变阵列或块保护寄存器。

### 12.5.3 Page 读取与 ECC

```text
PAGE_READ (0x13, 3-byte page)      ← 命令地址 = 原始页号（row = page），
                                       与原厂 spinand.c 及 Linux 主线一致
  → 轮询 STATUS.BUSY，超时 30000 × 1 µs
  → 解码 STATUS[5:4]
       00 = 无位翻转
       01 = 已纠错（数据有效）
       10 = 不可纠错（数据无效，fail closed）
       11 = 两份来源解码结论冲突（Luban-Lite 对部分 W25N04 系按
            "已纠 5-8 位"处理、W25N01GV datasheet 定义为保留）；
            本驱动仅信任 W25N01GV，统一按错误拒绝并打印状态字节
  → READ FROM CACHE (0x03, 2-byte column, 8 dummy cycles)
       column=0            : 主区，可附加 OOB
       column=page_size    : 纯 OOB
```

页加载（`PAGE_READ`）与缓存读取（`READ FROM CACHE`）两步的 QSPI 超时
`-RT_ETIMEOUT` 都会透传为 `SPINAND_RO_ETIMEOUT`，与传输失败
（`SPINAND_RO_EIO`）分开上报。

返回语义：

| 场景 | 原始 helper | MTD `read_page` |
|---|---|---|
| 正常 | `SPINAND_RO_OK` | `RT_MTD_EOK` |
| ECC 已纠错 | `SPINAND_RO_ECC_OK` | `RT_MTD_EECC_CORRECT` |
| 不可纠错 | `SPINAND_RO_EECC` | `RT_MTD_EECC` |
| 忙/传输超时 | `SPINAND_RO_ETIMEOUT` | `RT_MTD_EBUSY` |
| 传输失败 | `SPINAND_RO_EIO` | `RT_MTD_EIO` |
| 参数/越界错误 | `SPINAND_RO_EINVAL` | `-RT_MTD_EIO`（负值） |

**不会静默忽略 ECC 错误**：不可纠错时不返回页数据；已纠错时通过
`RT_MTD_EECC_CORRECT` 明确上报。参数/边界错误按树内惯例返回**负的**
`-RT_MTD_EIO`（对照 `bsp/nuvoton/libraries/nu_packages/SPINAND` 的
在树驱动写法），与传输失败（正值 `RT_MTD_EIO`）区分。

### 12.5.4 坏块只读检查

Winbond 系列出厂坏块标记位于块首页 OOB 第 0 字节：

- `0xFF`：好块；
- 其他值：坏块（原厂工具常用 `0xF0`；本驱动标坏时写 `0x00`）。

`check_block` 读取该字节，不写任何标记。返回约定：

- `RT_EOK`：好块；
- `-RT_ERROR`：检测到坏块；
- `RT_MTD_EIO` / `RT_MTD_EBUSY`：传输失败或忙超时，不能据此判定好坏块。

与 V1.3.3 一致，坏块检查只读取 OOB 标记，不以主区 ECC 位判断标记是否可读，
不关闭片内 ECC。常规数据页仍拒绝不可纠正/保留 ECC 状态；错误路径不更新调用者
的坏块判定结果。主机测试直接编译真实驱动，覆盖四种 ECC 状态、传输错误和超时。

### 12.5.5 写入、擦除、移动与标坏

| 操作 | NAND 命令序列 |
|---|---|
| 写页 | `WRITE_ENABLE 0x06` → `PROGRAM_LOAD 0x02`（2 B 列地址=0，整页镜像）→ `PROGRAM_EXECUTE 0x10`（3 B 页地址）→ 等待 BUSY |
| 擦块 | `WRITE_ENABLE 0x06` → `BLOCK_ERASE 0xD8`（3 B 页地址=块首页）→ 等待 BUSY |
| 移动页 | `PAGE_READ` 读源页到内部页缓冲 → 按写页序列写入目标页 |
| 标坏 | 擦块 → 以全 `0xFF` 主区 + `OOB[0]=0x00` 编程块首页 |

要点与约束：

- **目标页必须已擦除**。NAND 编程只能把位从 1 变 0；驱动不做隐式擦除，
  也不替调用者检查坏块。上层必须先 `check_block`，再按需
  `erase_block`，最后 `write_page`。
- 页镜像固定为 `page_size + oob_size`（W25N01GV 为 2048+64 B）。
  调用者未提供的主区/OOB 字节填 `0xFF`；片内 ECC 使能时，OOB 中
  ECC 校验位由芯片在编程时生成。
- 每次写/擦后检查 `STATUS`：BUSY(bit0)、`ERASE_FAIL`(bit2)、
  `PROG_FAIL`(bit3)。失败打印页号/块号和状态字节并返回错误。
- 超时与读路径一致：`spinand_wait_busy` 最多轮询 30000 × 1 µs。
- 第一次写/擦前读取 Block Lock `0xA0`；非 0 时写 0 并打印
  `cleared block protection`。若 WP# 引脚仍封锁，写/擦会以
  `PROG_FAIL`/`ERASE_FAIL` 报错，而不是伪装成功。
- `move_page` 复制主区与 OOB；源页 ECC 不可纠错时拒绝移动，避免把
  无效数据写到新页。
- `mark_badblock` 先擦除再写标记，因此对“人工标坏”的块再执行
  擦除会把 `OOB[0]` 恢复为 `0xFF`（标记不是 OTP）；出厂坏块的
  物理可靠性问题不会因此消失。驱动自身不扫描、不持久化 BBT。

写路径返回语义：

| 场景 | MTD `write_page` / `move_page` / `erase_block` / `mark_badblock` |
|---|---|
| 成功 | `RT_MTD_EOK` |
| 参数越界/长度非法 | `-RT_MTD_EIO`（负值） |
| 忙/传输超时 | `RT_MTD_EBUSY` |
| 编程/擦除失败 | `RT_MTD_EIO` |
| 只读策略（`BSP_SPINAND_READONLY=y` 的声明分区） | `-RT_EPERM` |

### 12.5.6 未分配尾部：`nand_scratch`

`image_cfg.json` 未声明的物理块可以在
`CONFIG_BSP_SPINAND_SCRATCH_MTD=y` 时注册为标准 MTD 设备
`nand_scratch`。它使用完整写路径，但范围被硬性限制在这些块内，
因此不可能通过它访问启动链。

当前 demo128 布局（spl 0-7、env 8-11、os 12-75、rodata 76-267、
data/data_r 268-907、refresh 908-915）下：

```text
nand_scratch = 物理块 916..1023，108 块，13.5 MiB
```

实机观察：首次测试前，抽查块 915/916/918/1023 的**首页**均带有
上一种固件遗留的 UBI EC 头（`55 42 49 23`），其后各页为 `0xFF`。这些块不属于当前
`image_cfg.json` 的任何分区，当前 Bootloader/RT-Thread 都不会访问；
`bsp.d213.spinand.rw` 在块 916 上完成擦除/编程/移动/再擦除后，该块
恢复为全 `0xFF` 且坏块标记仍为 `0xFF`。如果将来重新刷入基于 UBI 的
固件，需要重新格式化该区域。

`nand_scratch` 本身只是 MTD 分区。第二阶段在它上面增加了坏块映射层
和小型文件系统（12.5.8~12.5.9），但范围仍严格限制在这 108 个物理块
内，并且没有 Refresh 或后台维护。

### 12.5.7 只读 FAT 挂载（`rodata`）

原厂镜像把 `rodata` 分区写成 FAT 文件系统，而不是 FIT。本 BSP 在
`BSP_SPINAND_RODATA_FS=y` 时提供 `../libraries/drivers/storage/drv_nand_rodata.c`：

- 以 MTD `rodata` 为后端，注册 512 字节扇区的块设备 `nand_rodata`；
- 设备只有 `read`/`control` 路径：`write` 返回 0 扇区，
  `RT_DEVICE_CTRL_BLK_ERASE` 返回 `-RT_EPERM`；
- 下层 MTD 分区仍由 `BSP_SPINAND_READONLY=y` 拒绝 Program/Erase，
  因此即使绕过块设备也无法修改镜像；
- 单页缓存 + 互斥量，按页读取并按扇区拷贝；ECC 可纠正错误视为成功，
  不可纠正错误终止本次读取。

`board/storage.c` 在 `INIT_FS_EXPORT` 阶段先用
`dfs_mount(RT_NULL, "/", "rom", 0, &romfs_root)` 挂一个只读 romfs
作为根，提供 `/rodata`、`/nand`、`/dev` 挂载点；随后执行
`dfs_mount("nand_rodata", "/rodata", "elm", 0, RT_NULL)`。
DFS v1 要求非根挂载点在挂载前已存在，这是 romfs 根的唯一职责。

实机确认 `rodata` 起始页（物理页 4864）是合法 FAT12 BPB：
`EB 3C 90`、OEM `MTOO4048`、512 B/扇区、8 扇区/簇、2 个 FAT、
根目录项 2、`FAT` 标识在偏移 54/82。当前镜像的根目录为空，这是
合法状态，不能据此判定挂载失败；`ls /rodata` 返回空列表。

配置：

```text
CONFIG_BSP_SPINAND_RODATA_FS=y    # 依赖 RT_USING_DFS && !RT_USING_DFS_V2
```

### 12.5.8 持久化坏块表（BBT）

`../libraries/drivers/storage/drv_nand_lfs.c` 在 `nand_scratch` 的 108 个物理块上维护
持久化 BBT。它不依赖 Bootloader 的坏块跳过规则，也不触碰任何声明
分区。

布局（演示板 108 块）：

- 上电时对全部 108 块调用 `rt_mtd_nand_check_block()`，记录出厂坏块；
- 前两个好块作为 BBT 槽 0/1，交替写入，每次写另一个槽；
- BBT 记录放在槽块第 0 页，小端序：

```text
偏移  字段
0     "AICB" 魔数
4     版本 (1)
6     头部长度 (24)
8     sequence (u32，每次持久化 +1)
12    物理块总数 (u16)
14    LittleFS 逻辑块数 (u16)
16/18 BBT 槽 0/1 的物理块号 (u16)
20    备用块数 (u16)
22    保留
24    每个物理块的状态数组 (u8)：0=MAPPED, 1=BAD, 2=SLOT, 3=SPARE
24+n  逻辑到物理映射数组 (u16 × logical)
末尾  前面所有内容的 CRC32 (poly 0xEDB88320)
```

- 两个槽都通过 CRC、几何、状态和映射一致性校验；取序号较新的
  有效副本；
- 启动扫描时若某个已记录块现在被标记为出厂坏块，则在内存中把它
  标为 BAD：如果它是备用块，直接从备用池扣除；如果它已被映射或
  作为 BBT 槽，从备用池取一块替换并立即持久化；
- 运行期 `erase`/`program` 失败时把当前逻辑块标坏，从备用池
  取新块更新映射，并在返回 LittleFS 前持久化。擦除路径会在替换块
  上重试一次；编程路径返回 `LFS_ERR_IO`，由 LittleFS 处理；
- 备用池耗尽时该逻辑块标记为 `BBT_NO_BLOCK`，不可再访问；
  初始化阶段若连 4 个逻辑块都保不住则挂载失败。

已知边界：BBT 只有两份，写入过程中断电时另一份仍可用，但没有做过
掉电注入测试；BBT 不记录坏块产生的时间、不统计擦写次数；没有
Refresh 或后台扫描，运行期产生的坏块只在访问失败时被发现。

`nand_lfs_info` 打印 BBT 序号、两个槽的物理块号、活动槽、
逻辑/备用/坏块数量。

### 12.5.9 LittleFS 与 DFS 适配

文件系统使用未修改的 LittleFS v2.5（`littlefs/lfs.c`、
`lfs_util.c`、`lfs.h`，BSD-3-Clause，见 `LICENSE.md`）。BSP 只增加
`../libraries/drivers/storage/drv_nand_lfs.c` 中的适配层。

几何参数（物理页 2048 B、块 128 KiB、102 个逻辑块）：

```text
read_size    = prog_size = 2048
block_size   = 131072
block_count  = logical (演示板 102)
cache_size   = 2048
lookahead    = 32
block_cycles = 512
```

LittleFS 回调把逻辑块经 BBT 映射到物理块：
`read` 逐页调用 `rt_mtd_nand_read()`，ECC 可纠正继续、不可纠正返回
`LFS_ERR_CORRUPT`；`prog` 逐页调用 `rt_mtd_nand_write()`；`erase`
调用 `rt_mtd_nand_erase_block()`。所有对齐和范围都先检查。

DFS v1 文件系统 `lfs` 实现 mount/unmount/mkfs/statfs/unlink/stat/
rename 和 open/close/read/write/flush/lseek/getdents。LittleFS 默认
非线程安全，适配层用 BBT 互斥量串行化每个入口；RT-Thread 互斥量
可递归，坏块路径在文件操作内再次取锁是安全的。

启动时 `board/storage.c` 调用 `aic_nand_lfs_mount()`，把
LittleFS 挂到 `/nand`。首次挂载空白的 scratch 时会格式化；之后
`BSP_SPINAND_LITTLEFS_AUTOFORMAT` 只在 BBT 本次新建时允许自动
格式化，避免误擦已有卷。MSH：

```text
nand_lfs_info              # 查看块数、BBT、已用块
nand_lfs_format confirm    # 仅在 /nand 未挂载时格式化 scratch
```

`nand_lfs_format` 是唯一会写 NAND 的 MSH 命令，且只作用于
`nand_scratch`；没有针对 `rodata`、`os`、`os_r` 或 `data` 的写命令。

注意：LittleFS v2.5 的 `LFS_ASSERT` 会引用只在 `RT_DEBUG` 下编译的
`lfs_mlist_isopen()`。BSP 不为 littlefs 组定义 `RT_DEBUG`，因此通过
`littlefs/SConscript` 的 `LFS_NO_ASSERT` 关闭断言，不修改导入源码。

## 12.6 标准 MTD NAND 接入

### 12.6.1 上游接口与兼容层

使用上游 `components/drivers/include/drivers/mtd_nand.h` 的标准 7 个 ops，
不修改公共头文件，也不把原厂 13 槽扩展（`continuous_read`、块状态、
OOB map、bitflip 回调）塞进上游结构体。

| 标准 op | 实现 | 说明 |
|---|---|---|
| `read_id` | `spinand_mtd_read_id` | 重新读取 JEDEC ID |
| `read_page` | `spinand_mtd_read_page` | 绝对页 = `block_start*ppb + 分区相对页` |
| `write_page` | `spinand_mtd_write_page` | Program Load/Execute；声明分区在只读策略下返回 `-RT_EPERM` |
| `move_page` | `spinand_mtd_move_page` | 读源页再编程目标页；声明分区同上 |
| `erase_block` | `spinand_mtd_erase_block` | Block Erase；声明分区同上 |
| `check_block` | `spinand_mtd_check_block` | 只读坏块标记 |
| `mark_badblock` | `spinand_mtd_mark_badblock` | 擦块 + 写 `OOB[0]=0x00`；声明分区同上 |

所有 op 必须非空：上游 `mtd_nand.c` 对 `write_page` / `move_page` /
`erase_block` 使用 `RT_ASSERT`，置 `NULL` 会在调用时断言，而不是返回错误。
声明分区是否可写由注册时选择的 ops 表决定（只读表 / 读写表），而不是
运行时开关；`nand_scratch` 始终绑定读写表。

### 12.6.2 注册标志审查

上游 `rt_mtd_nand_register_device()` 固定以
`RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_STANDALONE` 注册。驱动在注册成功后
按策略改写 `device->parent.flag`：

- 声明分区且 `BSP_SPINAND_READONLY=y`（默认）：
  `RT_DEVICE_FLAG_RDONLY | STANDALONE`；
- 声明分区且 `BSP_SPINAND_READONLY=n`：
  `RT_DEVICE_FLAG_RDWR | STANDALONE`；
- `nand_scratch`：`RT_DEVICE_FLAG_RDWR | STANDALONE`。

**该标志只是自述，不构成保护。** 真正的保护来自注册时选择的 ops 表和
设备范围限制：

- 只读策略下，声明分区的 ops 表把 `write_page` / `move_page` /
  `erase_block` / `mark_badblock` 指向非空拒绝桩，返回 `-RT_EPERM`；
- `nand_scratch` 的 `block_start` / `block_end` 只覆盖未分配尾部，
  MTD 层无法越界到声明分区；
- 注册后把 `device->parent.ops` 换成本 BSP 的设备 ops：
  - 通用 `rt_device_read()` 返回 `-RT_ENOSYS`（防止把“返回 length”
    误判为页读取成功）；
  - 通用 `rt_device_write()` 返回 `-RT_EPERM`；
  - 通用 `rt_device_control()` 返回 `-RT_ENOSYS`：所有 NAND 访问必须走
    `rt_mtd_nand_read()` 等 MTD API，未知命令不得伪装成功；
- 不注册 `rt_blk_nand` 块设备；
- 不挂载任何文件系统；
- MSH 命令不提供任何写/擦/修复入口。

关闭 `BSP_SPINAND_READONLY` 会把**全部声明分区**变成可写，只应在
目标产品的分区策略、坏块处理和掉电行为验证完成后使用；本 BSP 的
默认配置和所有 `configs/*.config` 均保持 `y`。

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
由 `../libraries/soc/d21x/drivers/drv_spinand.c` 使用。即使将来修改分区，固件与打包配置也不会
出现两套偏移。

### 12.7.2 线性分区与 NFTL 分区

| 分区 | 类型 | 是否注册 MTD |
|---|---|---|
| `spl` | 线性 | 是（默认只读） |
| `env` / `env_r` | 线性 | 是（默认只读） |
| `os` / `os_r` | 线性 | 是（默认只读） |
| `rodata` / `rodata_r` | 线性 | 是（默认只读） |
| `data` / `data_r` | **NFTL** | **否**，仅物理 Page 只读 |
| `refresh` | 线性 | 是（默认只读） |
| 未分配尾部（916~1023） | 线性 | 仅 `BSP_SPINAND_SCRATCH_MTD=y` 时注册为可写 `nand_scratch` |

“默认只读”指 `BSP_SPINAND_READONLY=y` 时无法写/擦/标坏；
`refresh` 只是允许被读取，**不会**启动原厂 refresh 维护；驱动中没有
refresh 回调、没有后台线程、没有写回。`nand_scratch` 不属于任何
`image_cfg.json` 分区，不在启动链映射内。

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

### 12.7.4 `os`/`os_r` FIT 识别

`aic_nand_partitions` 通过标准 `rt_mtd_nand_read()` 读取 `os` 和 `os_r`
分区第 0 页的前 8 字节：

- magic = `0xD00DFEED`；
- totalsize 位于大端偏移 4，要求 `0x100 <= totalsize <= 分区大小`。

`aic_nand_get_fit_info()` 把结果封装为 `struct aic_nand_fit_info`，
同时返回实际 magic、totalsize、分区字节数和首页读取结果；页面读取
失败不伪装成“空分区”。`nand_report_slot_fit()` 对两个槽分别打印
`FIT image detected` 或 `no valid FIT header`。该检查用于证明分区
映射和读取链路对运行槽成立，而不是用“数据不全为 0xFF”作为成功证据。

`bsp.d213.ota` 的 `tc_ota_slot` 在实机断言 `os` 有有效 FIT 头，
`os_r` 允许为空；HIL `20261109-015923-59712` 观察到
`os` totalsize 729、`os_r` 为空。

### 12.7.5 只读 A/B 环境诊断

`board/ota_slot.c` 只读解析 Bootloader 的冗余环境镜像：

- `env`/`env_r` 各 4096 B：偏移 0 为小端 u32 CRC32，偏移 4 为
  u8 flags，偏移 5..4095 为 NUL 分隔的 `key=value`；
- CRC32 使用标准多项式 `0xEDB88320`，覆盖数据区 `bytes[5..4095]`；
- 读取时在块边界跳过出厂坏块，与厂商 `packages/artinchip/env`
  的加载规则一致；
- 两份都有效时按 FLAG_INCREMENTAL 规则选择活动副本：
  `flags0==255 && flags1==0` 选 1；
  `flags1==255 && flags0==0` 或 `flags0>=flags1` 选 0；否则选 1。

`aic_ota_env_status()`、`aic_ota_env_get()` 和 `aic_ota_slot_status`
提供 C/MSH 只读接口，报告 `osAB_now`、`osAB_next`、`rodataAB_next`、
`dataAB_next`、`upgrade_available`、`bootcount`、`bootlimit` 以及
两个槽的 FIT 状态。若 `osAB_next` 指向没有有效 FIT 的槽，命令打印
warning，但不修改任何分区。

边界：本驱动不写 `env`、不切换 `osAB_next`、不做版本比较/回滚，
也不解析 OTA 包；这些属于 Bootloader 契约的写入侧和上层 OTA 引擎。
`bsp.d213.ota` 已在实机 PASS（HIL `20261109-015923-59712`），
默认烧录只写 `os`，因此 `env` 与 `os_r` 保持原样。

## 12.8 配置

### 12.8.1 Kconfig

```text
BSP_USING_QSPI0              select RT_USING_SPI + RT_USING_QSPI
BSP_QSPI0_FREQ               默认 24000000
BSP_QSPI0_INPUT_HZ           Bootloader 已配置的 QSPI0 输入时钟，默认 100000000
BSP_USING_SPINAND_MTD_NAND   depends on BSP_USING_QSPI0
                             select RT_USING_MTD_NAND
BSP_SPINAND_READONLY         default y；为 y 时声明分区拒绝写/擦/标坏
BSP_SPINAND_SCRATCH_MTD      default n；把未分配尾部注册为可写 nand_scratch
BSP_SPINAND_RODATA_FS        depends on BSP_SPINAND_READONLY && RT_USING_DFS
                             && !RT_USING_DFS_V2；select ELMFAT/ROMFS；
                             在 /rodata 只读挂载 rodata 的 FAT 镜像
BSP_SPINAND_LITTLEFS         depends on BSP_SPINAND_SCRATCH_MTD &&
                             RT_USING_DFS && !RT_USING_DFS_V2；
                             select ROMFS；在 /nand 挂载 LittleFS
BSP_SPINAND_LITTLEFS_AUTOFORMAT       default y；BBT 新建时格式化空白卷
BSP_SPINAND_LITTLEFS_SPARE_BLOCKS     1..16，默认 4
```

DFS 必须先由用户在 RT-Thread 组件菜单中打开（选择 v1，不能是 v2）；
`select RT_USING_DFS` 会造成 Kconfig 循环依赖，因此驱动不自动打开 DFS。

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
cd bsp/artinchip/d213ecv-demo-v4-0
cp configs/d213-nand-readonly.config .config
scons --pyconfig-silent
scons -j8
```

`d213-utest.config` 除同一组开关外还加入：

```text
CONFIG_RT_USING_DFS=y
CONFIG_RT_USING_DFS_V1=y
CONFIG_DFS_USING_POSIX=y
CONFIG_DFS_USING_WORKDIR=y
CONFIG_RT_USING_DFS_ELMFAT=y
CONFIG_RT_USING_DFS_ROMFS=y
CONFIG_RT_USING_DFS_ROMFS_USER_ROOT=y
CONFIG_BSP_SPINAND_SCRATCH_MTD=y
CONFIG_BSP_SPINAND_RODATA_FS=y
CONFIG_BSP_SPINAND_LITTLEFS=y
CONFIG_BSP_SPINAND_LITTLEFS_AUTOFORMAT=y
CONFIG_BSP_SPINAND_LITTLEFS_SPARE_BLOCKS=4
```

因此标准 utest 固件同时包含 `bsp.d213.spinand.ro`（只读回归）、
`bsp.d213.spinand.rw`（`nand_scratch` 上的读写回归）和
`bsp.d213.spinand.fs`（`/rodata` 只读 FAT + `/nand` LittleFS/BBT）。
`d213-nand-readonly.config` 不启用 DFS、scratch 或文件系统，保持纯
只读镜像。

## 12.9 MSH 命令

`aic_nand_*` 与 `aic_ota_*` 命令只读；写/擦入口只作用于
`nand_scratch`，不接触声明分区。`nand_lfs_format confirm`、
`nand_lfs_bbt_test confirm` 和 `nand_lfs_stress start` 是例外，
它们只操作 `/nand` 所在的 scratch 区或其中的 BBT 槽。

| 命令 | 语义 |
|---|---|
| `aic_nand_info` | JEDEC ID、型号、Page/OOB/Block/容量、ECC 强度、QSPI 频率、策略与 scratch 范围 |
| `aic_nand_status` | 读 `STATUS(0xC0)` 与 `CFG(0xB0)`，解码 BUSY/ECC/配置位 |
| `aic_nand_read_page <physical_page>` | 读取整片物理页，打印 ECC 结果、主区前 32 B、OOB 前 16 B |
| `aic_nand_check_block <physical_block>` | 读取出厂坏块标记，输出 good/FACTORY BAD |
| `aic_nand_partitions` | 分区表、线性/NFTL 类型、坏块计数、`os`/`os_r` FIT 检测 |
| `aic_nand_read_part <name> <offset> <length>` | 从线性分区读取，offset 为分区内字节偏移，length ≤ 4096，输出 CRC32 与前 64 B |
| `nand_lfs_info` | LittleFS 逻辑/备用/坏块数、BBT 序号与两个槽、活动槽、已用块 |
| `nand_lfs_format confirm` | 在 `/nand` 未挂载时格式化 `nand_scratch`（必须带字面量 `confirm`） |
| `aic_ota_slot_status [env]` | 只读报告 env/env_r 副本、活动副本、A/B 键和 os/os_r FIT；带 `env` 时转储全部键 |
| `nand_lfs_bbt_test confirm` | 擦除当前活动 BBT 槽，重启后应从另一槽恢复（必须带字面量 `confirm`） |
| `nand_lfs_stress start [seconds]` | 循环写 `/nand/stress.bin`，用于配合 WDT 复位做复位/掉电注入 |
| `nand_lfs_stress check` | 读取并校验 `/nand/stress.bin` 头部和 4 KiB `0x5A` 载荷 |

边界规则：

- `read_page`：`page < block_count × pages_per_block`；
- `check_block`：`block < block_count`；
- `read_part`：只接受已注册的线性分区；`offset + length ≤ 分区大小`；
- 所有参数都做完整解析和范围检查，失败打印具体错误；
- 数值解析（`nand_parse_u32`）为手写严格解析：只接受十进制或 `0x/0X`
  十六进制，**拒绝正负号、空白、尾随非法字符和超出 32 位范围的值**
  （此前用 `strtoul` 会把负数/前导空白/截断值静默转换成错误的大数）。

启动日志示例（硬件实测后应替换为真实输出）：

```text
qspi0: registered, base=0x10400000 sclk=100000000 Hz requested=24000000 Hz actual=16666666 Hz cmu=0x........
spinand: pre-reset ID EF AA 21 00
spinand: reset ok, STATUS=0x00
spinand: JEDEC ID EF AA 21 FF
spinand: W25N01GV, 2048+64 B, 64 pages/block, 1024 blocks
spinand: geometry hardware-verified on this BSP
spinand: CFG=0x18 STATUS=0x00
spinand: 8 linear MTD partition(s) registered, read-only
spinand: nand_scratch = physical blocks 916..1023 (13824 KiB, writable)
```

`CFG=0x18` 表示 BUF_READ(bit3) + ECC_ENABLE(bit4) 已置位；
`STATUS=0x00` 表示空闲、无 ECC 事件。复位两次失败时将打印
`spinand: reset busy timeout` / `reset command failed`，并直接以
`-RT_ERROR` 结束初始化、不注册 MTD；未验证型号会打印
`geometry UNVERIFIED` 且分区不注册。

## 12.10 测试与验证

### 12.10.1 已执行（BUILD / STATIC / HOST）

#### 2026-11-09 第二阶段构建矩阵

| 项目 | 结果 | 说明 |
|---|---|---|
| `build-check.py` 全部 16 个 target | PASS | minimal/dma/dma-utest/uart-dma-utest/default/utest/nand-readonly/sdcard/sdcard-utest/sdio-utest/audio-utest/usb-utest/usbdev-utest/ce-utest/eth-utest/d126-minimal，build + ELF/map + package 全部 PASS（增加 CE target 后全量复测；BBT 双副本修复后再次 16/16 全量复测；之前为 15 个） |
| `d213-utest` | PASS | 22 个 `_utest_testcase`；ELF 检查要求 `bsp.d213.spinand.fs`、`bsp.d213.ota` 与 `nand_rodata` 字符串存在 |

首轮矩阵暴露出 `tc_nand_fs.c` 在未启用 DFS 的 utest 配置中无条件
包含 `dfs_fs.h`，导致 6 个 target 编译失败；已把 DFS 头文件移入
`BSP_SPINAND_RODATA_FS && BSP_SPINAND_LITTLEFS` 条件块，复测全部
PASS。

#### 2026-11-06 第二轮（可靠性修复后回归）

| 项目 | 结果 | 说明 |
|---|---|---|
| `d213-minimal` clean build + ELF/map | PASS | 无回归 |
| `d213-default` clean build + ELF/map | PASS | 无回归 |
| `d213-utest` clean build + ELF/map | PASS | 含 `bsp.d213.spinand.ro` 与 10 个 `_utest_testcase` 符号 |
| `d213-nand-readonly` clean build + ELF/map | PASS | QSPI/NAND/MTD + 分区生成 |
| `d126-minimal` clean build + ELF/map | PASS | 无回归 |
| 实机（d213-utest 固件） | **HW FAIL（已定位并修复）** | `CFG.CTRL_RST` 不自清被误判为致命；按原厂 HAL 语义改为警告并继续，复测 PENDING |
| `tools/test_nand_parts.py -v` | 4/4 PASS | 布局、对齐、越界、头文件生成 |

本轮代码改动：`drv_qspi.c`（复位/FIFO 超时检查：发现即报告，但按
原厂 HAL 语义不终止初始化；失败不注册总线的短路保留）、
`drv_spinand.c`（错误码区分/hw_verified 门控/复位严格化/严格参数解析/
`control` 返回 `-RT_ENOSYS`）、`utest/tc_spinand_ro.c`（缺设备即 FAIL、
分区一致性与 FIT 魔数用例）。构建命令：

```sh
$env:RTT_EXEC_PATH = '<SDK>\toolchain\bin'
python bsp/artinchip/tools/build-check.py --sdk <SDK> \
    --targets d213-minimal d213-default d213-utest d213-nand-readonly d126-minimal
python bsp/artinchip/d213ecv-demo-v4-0/tools/test_nand_parts.py -v
```

注意：`RTT_EXEC_PATH` 必须指向**工具链的 bin 目录**（含
`riscv64-unknown-elf-gcc.exe`），上级目录会触发 `building.py` 的
"无效工具链路径，删除 RTT_EXEC_PATH" 分支并失败。

#### 2026-11-06 第一轮（可靠性修复前基线）

| 项目 | 结果 | 说明 |
|---|---|---|
| `d213-minimal` clean build + ELF/map | PASS | 原有配置未回归 |
| `d213-default` clean build + ELF/map | PASS | 原有外设/显示未回归 |
| `d213-utest` clean build + ELF/map | PASS | 新增 `bsp.d213.spinand.ro` |
| `d213-nand-readonly` clean build + ELF/map | PASS | 新增 QSPI/NAND/MTD |
| `d126-minimal` clean build + ELF/map | PASS | 共享检查脚本路径分隔符修复后通过 |
| `tools/test_nand_parts.py` | 4/4 PASS | 布局、对齐、越界、头文件生成 |
| `tools/gen_nand_parts.py --check` | PASS | 128 MiB / 1024 block / 10 分区 |

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

##### 2026-11-06 第二轮实机结果（固件 `d213-utest`，修复前）

烧录第二轮 `d213-utest` 固件，启动正常（SPL 用同一 QSPI0 完成
183496 B 读并 CRC 通过），但 RT-Thread 中出现：

```text
qspi0: controller init failed (-116), bus not registered
qspi0: controller init failed (-116), bus not registered
spinand: qspi0 init failed (-116)
```

根因：`CFG.CTRL_RST` 在这片硅片上经常不自清（原厂 HAL 对同一位
轮询超时后仅 `pr_warn` 并继续，见 12.4.2）；本轮新增的"复位超时
即失败"检查过于严格，把健康控制器误判为死设备（`-116` =
`-RT_ETIMEOUT`，ETIMEDOUT=116 属 libc 错误码段）。

处理：`qspi_hw_init()` 改回原厂语义——轮询超时打印
`WARNING ... not self-cleared (CFG=0x%08x)` 后继续；签收该项为
**HW FAIL（修复）**，修复后固件复测 **PENDING**。

##### 2026-11-06 实机复测（修复后固件，实机日志）

烧录复位修复后的固件，实机启动日志（真实输出）：

```text
qspi0: registered, base=0x10400000 sclk=100000000 Hz requested=24000000 Hz actual=16666666 Hz cmu=0x00003105
spinand: pre-reset ID EF AA 21 00
spinand: reset ok, STATUS=0x00
spinand: JEDEC ID EF AA 21 00
spinand: W25N01GV, 2048+64 B, 64 pages/block, 1024 blocks
spinand: geometry hardware-verified on this BSP
spinand: CFG=0x19 STATUS=0x00
spinand: 8 linear MTD partition(s) registered, read-only
```

**HW PASS（初始化链路）**：QSPI0 注册成功、严格化复位成功一次即过
（`reset ok, STATUS=0x00` 是本轮新增的复位状态校验输出）、JEDEC/
几何/CFG/8 分区全部正常。注意：本次启动 CFG Reset 位自清成功
（未出现 WARNING），说明该位是否自清是**间歇性行为**，警告并继续
的语义是必要的。MSH/utest 各项仍为 PENDING（剩余项见下）。

##### 实机命令与期望值

实机命令与**本期计算出的期望值**（基于
`artifacts/d213-nand-readonly/images/d21x_os.itb`，88064 B 有效内容、
后续补零；坏块映射未知的限制见 12.7.3——若 Bootloader 不跳块烧写，
下列期望成立，若烧写工具跳过坏块，则需按实际块分布修正）：

1. `aic_nand_read_page 768`（`os` 起始物理页 = block 12 × 64）：
   - ECC 结果应为 clean（或 corrected）；
   - data 前 8 字节应为
     `d0 0d fe ed 00 00 02 d9`（FIT magic `0xD00DFEED`、
     totalsize 729 = FDT 头值，镜像已补零对齐；totalsize 不等于
     文件长度属正常现象）。
2. `aic_nand_read_part os 0 8`：与上一致；再加
   `aic_nand_read_part os 0 4096`：比对 CRC32。**CRC 期望值必须用
   实际烧录的那份 `d21x_os.itb` 现算**（例如 QSPI 复位修复后的
   本轮产物 90112 B，CRC = `0x818211a9`；此前两轮镜像分别为
   `0xde1e543a`/`0x6ee89a4a`，镜像大小与填充都会影响该值）：
   `python <repo>/fit_check.py artifacts/d213-nand-readonly/images/d21x_os.itb`
   （或用任意 zlib.crc32 工具）。
   magic/totalsize 与镜像内容弱相关，一般稳定；CRC 强相关，
   每次重新打包后都要更新期望值。
3. `aic_nand_partitions`：10 分区边界/spl 0-7/env 8-9/env_r 10-11/os
   12-43/os_r 44-75/rodata 76-171/rodata_r 172-267/data 268-587/
   data_r 588-907/refresh 908-915；8 个 MTD；`os` 行应显示
   FIT 识别（magic `0xD00DFEED` totalsize 729）。
4. `aic_nand_check_block 0` / `12`：期望 good；如 `os` 起始 12 个块
   内存在坏块且 Bootloader 跳过坏块烧写，FIT 探测会偏移——记录实际
   坏块分布再解读，不要臆断映射算法。
5.非法参数回归：`aic_nand_read_page 1024` / `aic_nand_check_block 1024` /
   `aic_nand_read_part os 0x400000 8` / `aic_read_page -1` /
   `aic_read_page 0x100000000` / 带尾随垃圾（如 `768x`）全部拒收；
   `aic_read_part os 0 0` 拒收（length=0）。
6. 写/擦/移动/标坏拒绝（utest 已断言；MSH 无写入口）。
7. 100 次复读（utest 已含；单测统计 clean/corrected 数）。
8. 冷启动、热重启各 ≥ 5 次，NAND 读取不改变启动结果；
   `spinand: reset ok`、`8 linear MTD partition(s) registered` 每次都在。
9. 确认 UART、tick、CAN、显示在含 NAND 的配置下仍正常。
10. 用逻辑分析仪抽查全程无 Program/Erase/Refresh 命令。

当前代码、构建与静态检查全部完成；以上实机步骤未执行，全部为
**HW PENDING**，没有编造任何日志。

#### 2026-11-08/09 写入路径实机验证（`nand_scratch`）

构建：`d213-utest` clean build + ELF/map + package PASS（12 个
`_utest_testcase`，新增 `bsp.d213.spinand.rw`）；`d213-nand-readonly`
保持 PASS（不含 scratch，只读文本检查不变）。

HIL 1：`20261108-225516-34668`，`--target d213-utest`，flash scope
`os`，suite `bsp.d213.spinand.ro` + `bsp.d213.spinand.rw`，status
**PASS**。

```text
tc_spinand: W25N01GV id=EF AA 21, 2048+64, 64 x 1024
tc_spinand: os FIT magic=0xD00DFEED totalsize=729 (partition 4194304 bytes)
tc_spinand: 100 repeat reads, 100 clean, 0 ECC-corrected
tc_nand_write: os partition rejects program/erase/mark
tc_nand_write: nand_scratch 108 blocks, testing block 0 (physical 916..1023)
tc_nand_write: block 0 erase/program/move/erase PASS (2048-byte page, 64-byte OOB)
[  PASSED  ] testcase (bsp.d213.spinand.ro)
[  PASSED  ] testcase (bsp.d213.spinand.rw)
```

HIL 2（加入标坏用例）：`20261108-225807-31876`，suite
`bsp.d213.spinand.rw`，status **PASS**：

```text
tc_nand_write: block 0 mark-bad/erase-marker PASS
```

最终复核（源码定稿后 clean rebuild）：`20261108-230606-16252`，
suite `bsp.d213.spinand.ro` + `bsp.d213.spinand.rw`，status
**PASS**；测试结束再次确认块 916 首页主区与 OOB 全 `0xFF`，
`os` 页 0 仍为 FIT 头 `d0 0d fe ed`。

测试内容（`nand_scratch` 块 0，物理块 916）：

1. 擦除整块并逐页回读，主区与 OOB 均为 `0xFF`；
2. 编程页 3 为固定图案，回读逐字节一致；
3. 邻页仍为擦除态（编程未扰动相邻页）；
4. `move_page` 页 3 → 页 4，回读一致；
5. 再次擦除整块并逐页回读为 `0xFF`（测试结束不留数据）；
6. `mark_badblock` → `check_block` 返回 `-RT_ERROR`；再擦除 →
   `check_block` 恢复 `RT_EOK`；
7. 只读策略下 `os` 分区的写/擦/移动/标坏均返回 `-RT_EPERM`。

测试后串口复核：

```text
aic_nand_info       : scratch : nand_scratch, physical blocks 916..1023
aic_nand_check_block 916 : physical block 916: good (OOB[0]=0xFF)
aic_nand_read_page 58624 : clean, 主区与 OOB 全 0xFF
aic_nand_read_part os 0 8: d0 0d fe ed 00 00 02 d9（FIT 头未受影响）
list device        : nand_scratch / refresh / rodata_r / rodata / os_r /
                     os / env_r / env / spl 均为 MTD Device
```

**结论：写/擦/移动/标坏命令序列与状态位处理 HW PASS（仅限未分配
尾部）。** 声明分区仍为只读；持久化 BBT、掉电循环、文件系统和 OTA
不在本轮范围内。

#### 2026-11-09 文件系统层实机验证

构建：`d213-utest` clean build + ELF/map + package PASS（13 个
`_utest_testcase`，新增 `bsp.d213.spinand.fs`）；`d213-nand-readonly`
仍 PASS，且不包含任何 DFS/文件系统代码。

HIL：`20261109-000938-77752`，`--target d213-utest`，suite
`bsp.d213.spinand.fs`，status **PASS**：

```text
tc_nand_fs: /rodata mounted, 0 root entries
tc_nand_fs: nand_scratch 108 blocks, 102 logical, 4 spare, 0 bad, BBT seq 1
tc_nand_fs: persistence boot=3
[I/utest] [  PASSED  ] testcase (bsp.d213.spinand.fs)
```

测试内容：

1. `/rodata` 的 `dfs_statfs` 成功，块大小 512；`nand_rodata` 的
   GETGEOME 为 512 B 扇区；扇区 0 具备 `0x55AA` 和 `FAT` 标识；
   只读块设备拒绝写（返回 0 扇区）和擦除（`-RT_EPERM`）；
2. `/nand` 的 `dfs_statfs` 成功；`nand_lfs_info` 的
   `logical + spare + bad + 2 == total`，两个槽不同且在范围内；
3. 创建 `/nand/utest`，写入 4 KiB 图案并逐字节回读，`stat` 大小为
   4096，重命名后旧名消失、新名存在，删除后目录为空并可移除；
4. 持久化标记 `/nand/persist.bin` 的计数在每次运行后递增：跨
   重新烧录 `os` 与复位，依次观察到 `boot=1 → 2 → 3 → 4`，
   证明 LittleFS 数据与 BBT 映射跨重启保持；
5. 测试结束 `nand_scratch` 仍为 108 块、0 坏块，声明分区未受影响。

串口手动复核：

```text
mount        : rom /；elm nand_rodata /rodata；lfs /nand
ls /nand     : persist.bin
ls /rodata   :（空，FAT 根目录为空）
nand_lfs_info: 108 blocks, 102 logical, 4 spare, 2 BBT, 0 bad
               BBT seq 1, slots 0/1, active 1
```

最终全量回归：`20261109-002129-38512`（`d213-utest` 全部套件，
flash scope `os`）：`bsp.d213.spinand.fs`、`.ro`、`.rw` 及其余
BSP 套件全部 PASS（`bsp.d213.*` 15/16），仅已知失败
`bsp.d213.fpu.context`；`bsp.d213.spinand.fs` 输出
`persistence boot=5`，即计数在本次烧录/复位后继续递增。

#### 2026-11-09 OTA/A-B 只读诊断实机验证

构建：`d213-utest` build + ELF/map + package PASS（22 个
`_utest_testcase`，新增 `bsp.d213.ota`）。

HIL：`20261109-015923-59712`，`--target d213-utest`，suites
`bsp.d213.ota`、`bsp.d213.spinand.fs`，status **PASS**；
flash scope `os`（只写运行槽，保留 `env`/`env_r`/`os_r`）：

```text
tc_ota: osAB_now=A from env
tc_ota: osAB_next=A
tc_ota: os FIT totalsize 729, partition 4194304 bytes
tc_ota: os_r is empty
tc_ota: running from osAB_now=A (os)
[I/utest] [  PASSED  ] testcase (bsp.d213.ota)
tc_nand_fs: nand_scratch 108 blocks, 102 logical, 4 spare, 0 bad, BBT seq 1
tc_nand_fs: persistence boot=6
[I/utest] [  PASSED  ] testcase (bsp.d213.spinand.fs)
```

`tc_ota_env` 断言活动 env 副本有效、`osAB_now`/`osAB_next` 为 A/B、
`upgrade_available` 为 0/1、`bootlimit>0`、`bootcount>=0`。
`tc_ota_slot` 断言 `os` FIT 有效，并允许 `os_r` 为空。

2026-11-09 实机已执行 `aic_nand_partitions` 与
`aic_ota_slot_status` 的 MSH 文本输出：10 个分区均为 0 bad/0 err，
`os` 的 FIT 头有效（magic `0xD00DFEED`，totalsize 729 字节），
`os_r` 无有效 FIT；env/env_r 均 valid，`osAB_now=osAB_next=A`，
`upgrade_available=0`，`bootcount=0`，`bootlimit=5`。BBT 槽擦除与
看门狗复位注入见下节，已 HW PASS。

#### 统一验证工具：BBT 双槽切换与复位注入

`tools/d213_storage_soak.py` 复用 `d213_hil.Console`，在已烧录
`d213-utest` 的板上执行：

```sh
python bsp/artinchip/tools/d213_storage_soak.py --port COM9 \
    --confirm-board d213-demo128 --confirm-storage
```

它先采集 `nand_lfs_info`、`aic_ota_slot_status` 和
`aic_nand_partitions`，然后擦除活动 BBT 槽并用 `wdt_test start 2`
复位，校验序号减一、活动槽翻转、`/nand` 重新挂载；随后重复
`wdt_test start 3` + `nand_lfs_stress start 30` 复位，最后复查
`/nand/stress.bin` 头部与 4 KiB 载荷、BBT、env 和 `os` FIT。工具只操作
`nand_scratch`，不写声明分区；`--plan` 只打印步骤，`--skip-bbt`
跳过破坏性的单次 BBT 槽擦除。2026-11-09 实机执行：
`--stress-cycles 2 --stress-seconds 30` 全流程 PASS（报告
`bsp/artinchip/artifacts/d213-storage-soak/20261109-035532`）。
基线 BBT seq 2/active 1，擦除 slot 1 后从 slot 0 恢复为 seq 1/active 0；
两轮看门狗复位分别发生在 stress seq 39 和 42，`/nand` 每次都重新挂载，
`/nand/stress.bin` 头部和 4 KiB 载荷校验通过，最终 env 与 `os` FIT 仍有效。

该次执行还暴露并修复了一个真实缺陷：`bbt_build_fresh()` 在
`active_slot < 0` 时只写第一个槽，导致首次创建的 BBT 没有冗余副本；
擦除唯一有效槽后驱动会误判为空白 scratch 并重建 BBT（若原映射非
恒等，会丢失 LittleFS 数据）。现在首次创建连续写两个槽，且
`nand_lfs_bbt_test confirm` 会先读取另一槽，另一槽无效时拒绝擦除。

### 12.10.3 结果分级

统一使用：

`BUILD / STATIC / HOST / HW / PENDING / FAIL`

当前结论：**BUILD + STATIC + HOST PASS；核心读取链路 HW PASS；
写/擦/移动/标坏在未分配尾部 `nand_scratch` 上 HW PASS
（2026-11-08/09，HIL `20261108-230606-16252`，另见
`20261108-225516-34668` / `20261108-225807-31876`）；
`/rodata` 只读 FAT 与 `/nand` LittleFS + 持久化 BBT 均 HW PASS
（HIL `20261109-000938-77752`；全量回归 `20261109-002129-38512`）；
OTA/A-B 只读诊断（env 冗余副本、FLAG_INCREMENTAL、os/os_r FIT）
HW PASS（`bsp.d213.ota`，HIL `20261109-015923-59712`）；
分区/FIT MSH 输出、BBT 双槽切换与看门狗复位注入 HW PASS
（`d213_storage_soak`，报告 `20261109-035532`）。
声明分区写入、冷启动掉电测试和 OTA 更新引擎仍为 PENDING。**

## 12.11 风险与限制

1. **坏块映射未知**：见 12.7.3。线性分区读取只保证“按 image_cfg 偏移读取”，
   不保证与 Bootloader 跳过坏块后的逻辑地址一致。
2. **仅 Winbond 子集**：实际板卡若为其他厂商，需按 JEDEC ID 增加型号表和
   ECC 解码；未知 ID 会拒绝注册分区。
3. **写入范围受限**：写路径、BBT 和 LittleFS 都只在未分配尾部
   `nand_scratch`（916~1023）完成实机验证；声明分区默认只读。
   因此**不能**据此声称 `data`/OTA 可用。
4. **坏块处理仅限 scratch**：MTD 层对声明分区仍只按标记判断，不扫描
   全片、不迁移数据；只有 LittleFS 层通过 BBT 做运行期坏块替换。
   没有 1000 次掉电循环，也没有坏块注入测试。
5. **BBT 冗余**：双槽交替 + CRC 能容忍单次更新中断电；首次创建现在会
   写满两个槽，单槽恢复路径已在 `nand_scratch` 上用擦除 + 3 次看门狗
   复位验证。但未做 1000 次掉电循环或坏块注入，备用池默认只有 4 块，
   连续坏块仍会导致卷缩小或挂载失败。
6. **单线同步**：数据阶段固定 1 线，读取速度低于 Quad；未使用 DMA。
7. **未验证电源/时序**：QSPI RX 采样固定为原厂默认的半周期模式，仍需示波器
   或逻辑分析仪在目标板上确认。
8. **CFG 写操作**：初始化会通过 SET_FEATURE 修改易失配置寄存器（BUF/ECC），
   第一次写/擦前会清除块保护 `0xA0`；这些都只涉及易失配置寄存器，
   不改变阵列数据（写/擦命令本身除外）。
9. **尾部遗留数据**：916~1023 在首次使用前带有上一种固件的 UBI 头，
   不属于本固件的任何分区；LittleFS 首次格式化会覆盖该区域。

## 12.12 后续实现文件系统的技术阻碍

1. 确认 Bootloader 的坏块跳过/分区映射规则，否则写入会破坏启动链；
2. ~~实现 Program Load / Program Execute / Block Erase / Mark Bad，并区分
   program/erase fail~~ **已完成**：`bsp.d213.spinand.rw` 在
   `nand_scratch` 上实机 PASS；
3. ~~设计坏块表持久化与掉电一致性~~ **已在 `nand_scratch` 完成**：
   双槽 BBT + CRC + 备用块替换，跨重启验证；尚未做掉电注入和
   1000 次循环；
4. 处理 ECC 阈值与位翻转上报，决定何时迁移数据（当前把可纠正 ECC
   当成功，不主动迁移）；
5. 在非启动分区（例如 `data`）做写入/擦除/断电循环；必须先备份并
   获得明确授权；
6. ~~引入 LittleFS 或 FatFs 前，先完成块设备层和 1000 次掉电测试~~
   **部分完成**：BBT 映射层、LittleFS 和 DFS v1 已在 `nand_scratch`
   上工作；在 `data` 上复用前仍需先解决 1、5、7；
7. `data`/`data_r` 的 NFTL 兼容性需要原厂库或重新设计，不能直接当线性
   块设备使用；
8. OTA 需要 A/B 分区、签名/CRC、回滚策略，不能只依赖 `os_r` 存在。

## 12.13 安全规则（保持有效）

写入路径已在 `nand_scratch` 上验证，但以下规则继续有效：

- 擦除/写入 `spl`、`bootloader`、`env`、`os`；
- 写入 Bootloader 地址 `0x47B80000`；
- 修改 `pack/image_cfg.json` 的分区偏移/名称/大小；
- 在 RT-Thread 中对整片 NAND 做擦除；
- 关闭 ECC 或忽略坏块标记；
- 在声明分区上启用 BBT 持久化、Refresh 或后台维护（`nand_scratch`
  的 BBT 已实现，见 12.5.8，不属于此限制）；
- 把 `data`/`data_r` 当作线性块设备写入。

破坏性写入只允许针对 `nand_scratch`（当前为物理块 916~1023）。
`nand_lfs_format confirm` 也只会擦除这个范围。
不得为了“测试写路径”关闭 `BSP_SPINAND_READONLY` 后对声明分区执行
写/擦/标坏，也不得把 `nand_scratch` 的边界改成覆盖任何声明分区。

## 12.14 SD 卡：SDMC1 + 标准 MMCSD 栈

### 12.14.1 当前状态

| 项目 | 结果 |
|---|---|
| 构建/静态检查/打包 | PASS（`d213-sdcard`、`d213-sdcard-utest`，含 IDMA 符号检查） |
| 控制器初始化 | HW PASS（`sdmc1: driver loaded (4-bit, IDMA polling, SDIO IRQ armable)`） |
| 卡识别 | HW PASS（`SD card capacity 15558144 KB`，SDR25 高速模式） |
| 块设备注册 | HW PASS（`list device` 可见 `sd0`、`sd0p0`） |
| CPU FIFO 只读 | HW PASS（`bsp.d213.sdcard`，8 扇区重复读，2026-11-07） |
| IDMA 数据面 | HW PASS：DMA 写入内存的数据正确（扇区 0 的 `0x55AA` 签名），控制流修复后拷回、CMD12 与完成判定均通过 |
| IDMA 64 扇区回归 | HW PASS（2026-11-07，`d213-sdcard-utest` + `bsp.d213.sdcard`，8 个 4 KiB 描述符，校验 `0x55AA`） |
| FAT 挂载 | HW PASS（`d213-sdcard`：14.856 GB 分区挂载到 `/`，`ls`/`df`/`cat` 通过，2026-11-07） |
| 顺序读吞吐 | HW PASS（`sd_perf 1024`：IDMA 24 ms / 512 KiB ≈ 21.8 MB/s，FIFO 23 ms ≈ 22.8 MB/s；受 50 MHz 4-bit 总线限制，IDMA 的 bounce 与 Cache 维护抵消了搬运收益） |
| SDIO 主机（SDMC2） | 标准 SDIO 栈 + RTL8733BS 模组：枚举、CCCR/FBR/CIS、CMD52、attach_irq 实机 PASS；CMD53 数据阶段需模组原厂上电序列，BSP 单独不可用，见 12.15 |
| 热插拔 | 不支持（插槽无 CD 引脚） |

实机启动日志（2026-11-07，IDMA 固件）：

```text
[aic] sdmc1: sclk 50000 kHz, parent 600000 kHz
[aic] sdmc1: driver loaded (4-bit, IDMA polling, SDIO IRQ armable)
[I/SDIO] SD card capacity 15558144 KB.
[I/SDIO] SD card switch to High Speed / SDR25 mode
found part[0], begin: 1048576, size: 14.856GB
[aic] sdmc1: FAT filesystem mounted at /
```

utest 结果（FIFO 基线，2026-11-07）：

```text
tc_sdcard: block size 512, sector count 31116288
[I/utest] [  PASSED  ] testcase (bsp.d213.sdcard)
```

IDMA 调试运行中，DMA 目标缓冲区的扇区 0 签名为 `0x55AA`，说明搬运本身
正确；该次运行暴露的控制流错误已经修复，最终 64 扇区回归等待实机复测。

FAT 读取结果：

```text
msh /> ls /
Directory /:
.fseventsd           <DIR>
.Spotlight-V100      <DIR>
...
msh /> df
disk free: 14.7 GB [ 30883120 block, 512 bytes per block ]
msh /> cat /UniversalJIT26.js
...
msh />
```

`list device` 输出：

```text
sd0p0            Block Device         0
sd0              Block Device         0
```

### 12.14.2 硬件资源与引脚

| 项目 | 参数 |
|---|---|
| 控制器 | SDMC1，基址 `0x10450000`，IRQ 47（仅 SDIO 中断源使用） |
| 引脚 | PC.0=CLK, PC.1=CMD, PC.2..PC.5=DAT0..DAT3，功能 2 |
| 上下拉/驱动 | 上拉，驱动强度 3 |
| 总线宽度 | 4-bit |
| 时钟 | 父时钟 PLL_FRA0 600 MHz；SDMC 分频后 100 MHz；控制器 sclk 50 MHz |
| 卡检测 | 无独立 CD 引脚；不做热插拔 |
| 冲突 | PC.0..PC.6 与 PWM0..PWM3 共用，Kconfig 用 `depends on !BSP_USING_PWM` 阻止同时启用 |

### 12.14.3 软件结构

- `../libraries/soc/d21x/hal/hal_sdmc.c`、`../libraries/soc/d21x/include/hal_sdmc.h`：控制器寄存器、
  命令/响应、FIFO、IDMA 描述符、时钟/相位/位宽，移植自 Luban-Lite
  （Apache-2.0）；
- `../libraries/soc/d21x/drivers/drv_dma.c`、`drv_dma.h`：通用 Cache 行对齐 DMA 缓冲
  分配和 clean/invalidate 封装，供 SDMC 及后续 USB/Audio 复用；
- `../libraries/soc/d21x/drivers/drv_sdmc.c`：注册 `rt_mmcsd_host`，块设备由标准
  `components/drivers/sdio` 的 MMCSD/block 层创建，名称为 `sd0`；
- `utest/tc_sdcard.c`：只读回归 `bsp.d213.sdcard`。

传输路径：

- 数据 ≥ 512 B（或长度不是 4 的倍数，例如 SDIO CMD53 字节模式）走
  IDMA；其余走 CPU FIFO；
- IDMA 使用 4 KiB 描述符，调用方缓冲区先复制到 64 B 对齐的 bounce
  buffer。写卡前 `clean`，读卡前后 `invalidate`；
- `DAT_DONE` 后还要等最后一个描述符的 `OWN` 位被 IDMAC 清零，避免
  CPU 在 DMA 尚未清空 FIFO 时读取旧数据；
- IDMA 连续失败 3 次后自动切回 FIFO，`sd_dma 1` 可重新启用；
- 命令/数据完成仍然轮询；只有 SDIO “卡请求注意”源接到 PLIC IRQ 47。

`SMCC_INTEN` 只打开 `FROM_SDIO`，ISR 也只清这一位并调用
`sdio_irq_wakeup()`，不读取、不清除命令/数据状态位：这些位由轮询路径
负责，若在 ISR 里一并清除会吞掉轮询侧正在等待的完成/错误事件。IDMA
描述符准备在 `blksize` 或块数为 0 时直接返回 0，避免 `4096 / blksize`
除零；`drv_sdmc.c` 把 0 个描述符当作非法请求，置 `-RT_EINVAL` 并走控制器
错误恢复流程。

仍然不实现：

- 热插拔/卡检测线程（无 CD 引脚）；
- UHS/DDR50/SDR104、eMMC（SDMC0）；
- SDIO 卡的真实功耗/时钟门控调优（没有 SDIO 硬件）。

多块读的收尾：控制器报告 `DAT_DONE` 后，卡仍处于 open-ended 数据模式，
需要发送 CMD12（`req->stop`），否则 `CTRST.BUSY` 不会清零，下一次
CMD18 会报 “controller busy”。数据阶段出错时也会先尽力发 CMD12，再
软复位控制器。

错误恢复：命令完成超时、数据错误或 DMA 错误后，`aic_sdmc_recover()`
执行 `hal_sdmc_soft_reset()`、恢复位宽/DDR/相位/FIFO 和时钟，避免
`CTRST.BUSY` 永久卡死。探测阶段的响应超时（例如 SD 卡不响应 CMD5）
不触发复位，这是正常的卡类型探测流程。

### 12.14.4 配置

| 符号 | 说明 |
|---|---|
| `BSP_USING_SDMC1` | 使能 SDMC1；`select RT_USING_SDIO`（进而 `select RT_USING_BLK`）并 `select BSP_USING_DMA` |
| `BSP_SDMC_USE_IDMA` | 默认 y；使用控制器内部 DMA，关闭后全部走 CPU FIFO |
| `BSP_USING_DMA` | 默认 y；编译 `libraries/soc/d21x/drivers/drv_dma.c` 的通用 DMA 缓冲接口 |
| `BSP_SDCARD_AUTOMOUNT` | 启动后尝试以 elm-chan FAT 挂载；不格式化卡 |
| `BSP_SDCARD_MOUNT_POINT` | 默认 `/` |
| `RT_MMCSD_STACK_SIZE` | 两个 SD 配置固定为 8192；RV64 下默认 2048 会在重新检查已插入卡时栈溢出 |
| `RT_SDIO_STACK_SIZE` | 两个 SD 配置固定为 8192；SDIO 中断线程同步走完整请求路径，默认 2048 余量不足 |

可复现配置：

```text
configs/d213-sdcard.config         SD 卡 + automount（最小配置基线）
configs/d213-sdcard-utest.config   utest + SD 卡（PWM 关闭）
```

### 12.14.5 命令与使用

```text
sd_rescan    仅当插槽为空（host->card == NULL）时触发一次探测
sd_mount     在 automount 配置下手工挂载
sd_dma       查看/切换 IDMA（sd_dma 0|1），并显示 dma/fifo 请求计数
sd_perf N    从扇区 0 顺序读 N 个块并打印吞吐（1..4096）
```

`sd_rescan` 在卡已识别时只打印提示并返回。原因是 MMCSD 核心的
`mmcsd_change()` 对已插入卡表示“卡已移除”，会释放卡和块设备；本移植
没有卡检测引脚，不支持安全的热替换。换卡后请重启板子。

若启动时无卡，插入卡后执行 `sd_rescan`，MMCSD 检测线程会在后台完成
识别并注册 `sd0`。

### 12.14.6 验证方法与注意事项

```sh
python bsp/artinchip/tools/build-check.py \
    --targets d213-sdcard d213-sdcard-utest \
    --sdk /path/to/luban-lite
```

utest 通过 `uart0` 执行：

```text
utest_run bsp.d213.sdcard
```

无卡时用例打印 `no SD card detected (SKIP ...)` 并返回；这**不是**卡路径
通过的证据。有卡时应先看到块设备 `sd0`，再读取 64 个扇区两次并比较，
同时校验扇区 0 的 `0x55AA` 启动签名，避免“两次都读到全零”被误判为
通过。

注意：

- utest 的两个 32 KiB 缓冲区必须为 `static`，否则会超过
  `RT_UTEST_THR_STACK_SIZE`（默认 8 KiB）；
- 64 扇区正好覆盖 8 个 4 KiB IDMA 描述符，用来验证多描述符链；
- 只读测试不写卡，不修改分区表；
- 烧录只写 `image.target.os`，不碰 SPL/env/Bootloader。

### 12.14.7 SDIO Host 能力与差距

本移植把 SDMC1 作为通用 SDIO Host 注册到 RT-Thread 的
`components/drivers/sdio`，不包含任何 Wi-Fi/CarPlay 私有协议或第三方
驱动。能力边界如下。

已实现（CODE + BUILD，无实机 SDIO 卡）：

- CMD5 探测：成功响应后置 `host.is_sdio`，后续 CMD52/CMD53 与 CIS/功能
  初始化全部由标准 SDIO 核心完成，驱动只提供通用 `request`；
- SDIO 带内中断：`INTEN.FROM_SDIO` → PLIC IRQ 47 → `sdio_irq_wakeup()`
  → SDIO 核心的 `sdio_irq` 线程；`enable_sdio_irq()` 同时开关
  `INTEN.FROM_SDIO` 和 `HCTRL1.INT_EN`，并在 SDIO 模式下取消
  `CLKCTRL.LOW_PWR`，保证异步唤醒时卡时钟仍在运行；
- 1-bit/4-bit 位宽通过标准 `set_iocfg` 切换；
- CMD53 块模式与字节模式：长度不是 4 的倍数（例如 2 字节的
  `sdio_io_readw()`）走 IDMA bounce，避免 FIFO 只支持整字的限制；
- `RT_SDIO_STACK_SIZE` 提高到 8192。SDIO 中断线程会同步执行
  `sdio_io_readb()` → `mmcsd_send_cmd()` → 本驱动的完整请求路径，与
  已实机暴露栈溢出的 MMCSD 线程同量级；2048 的默认值不应再使用。

与“完整 SDIO Host”的差距：

| 项目 | 状态 |
|---|---|
| UHS-I / 1.8 V 电压切换 | 未实现；只有 3.3 V 信令，SDIO 最高 SDR25 级别 |
| SDR50/SDR104、DDR50 | 未实现（与 UHS 电压切换绑定） |
| SDIO 挂起/恢复（SDIO_PM） | 未实现；RT-Thread PM 未接入 |
| 带外中断（OOB IRQ） | 未实现；只支持 SDIO 带内中断 |
| 多 SDIO 功能（function 1..7） | 标准核心支持，未实测 |
| 热插拔/卡检测 | 不支持（插槽无 CD 引脚） |
| 异步命令/数据完成 | 未实现；完成仍轮询，中断只服务 SDIO 唤醒 |
| 4-bit 与 1-bit 动态切换 | 走标准 `set_iocfg`，未实测 |
| 实机功能验证 | 无 SDIO 设备，全部为 CODE + BUILD |

因此可以确认的是“标准 SDIO 请求路径已打通、可被标准函数驱动调用”，
不能确认的是任何具体 SDIO 模组的枚举、吞吐和中断时延。接入 Wi-Fi 等
具体协议栈时，应保持本驱动只实现 Host 侧，不把私有固件/协议放进
`bsp/artinchip/`。

本节的“无实机 SDIO 卡”已被 12.15 取代：SDMC2 上已有 RTL8733BS 模组，
枚举与 CMD52 实机通过，CMD53 数据阶段仍未通过。

### 12.14.8 后续工作

1. IDMA 64 扇区回归、FAT 挂载和 `sd_perf` 已实机通过（2026-11-07）；
   仍需做至少 100 次重复读与一次插拔/重启循环；
2. 评估零拷贝：当调用方缓冲区本身 64 B 对齐且长度为 64 B 的倍数时，
   直接 DMA，省掉 bounce 复制；当前为简化错误恢复仍始终复制；
3. 若产品需要热插拔，增加 CD GPIO 或定期轮询，并重新设计
   `mmcsd_change()` 的移除/重探测流程；
4. 有 SDIO 模组后验证 CMD52/CMD53、SDIO 中断和 1-bit/4-bit 切换；当前
   代码路径已接入标准 SDIO 栈，但没有任何 SDIO 实机证据；
5. 增加写卡/掉电回归前，先在非重要卡上验证写入、同步和错误恢复。

## 12.15 SDIO 模组：SDMC2 + RTL8733BS

D213ECV-DEMO-V4-0 的 Wi-Fi/蓝牙模组接在 **SDMC2** 上。本节记录这一路
SDIO 的硬件连接、驱动结构和 2026-11-08 的实机结果。结论是：**主机侧
枚举与 CMD52 已实机通过，CMD53 数据阶段仍失败**，因此不能据此声称
Wi-Fi/蓝牙可用。

### 12.15.1 硬件资源与引脚

来源：板卡原理图第 7 页（`06-WIFI_SPEAKER_DMIC`）与 GPIO 分配表。

| 项目 | 连接 | 说明 |
|---|---|---|
| 控制器 | SDMC2 `0x10460000`，PLIC IRQ 48 | 只做 SDIO，不做 SD 卡 |
| SDIO 总线 | PF.0..PF.5，功能 2，上拉，驱动 3 | 原理图：SDC2_D1/D0/CLK/CMD/D3/D2 |
| WL_REG_ON | PD.1（WIFI_PWR_ON） | 模组 12 脚，板上有 R131 10 kΩ 下拉，必须主动拉高 |
| WL_HOST_WAKE | PD.2 | 低功耗唤醒，本 BSP 未使用 |
| BT_RST_N | PD.3（BT_PWR_ON） | 模组 34 脚；电平有效复位，需拉高 |
| BT_HOST_WAKE / BT_WAKE | PD.4 / PD.5 | 本 BSP 未使用 |
| BT HCI | UART7 PF.6..PF.9（TX/RX/RTS/CTS，功能 5） | 端口已接入（`BSP_USING_UART7` + 自动流控），协议栈不在本仓库，见 `07-uart.md` 7.11 |
| 供电 | VCC_WIFI 3.38 V（U21 SY8089，EN 由 VCC_LCD 经 R152 1 kΩ 上拉） | 显示上电即给模组供电 |
| LPO | 模组 24 脚 ← WIFI_32K | 由 SoC 提供，本 BSP 未配置 |

模组 SDIO 厂商/产品 ID 实测为 `0x024c / 0xb733`（Realtek，RTL8733BS）。
原理图丝印按 RTL8723DS 标注，两者同族封装；以实测 ID 为准。

### 12.15.2 驱动结构

`../libraries/soc/d21x/drivers/drv_sdmc.c` 一份驱动同时支持两个实例，配置在 `g_sdmc_cfgs[]`：

- SDMC1：PC.0..PC.6，`blk_name` 为 `sd0`，注册块设备；
- SDMC2：PF.0..PF.5，`blk_name` 为 `sd1`，**不注册块设备**（SDIO 卡
  在标准 MMCSD 核心里不会被当作块设备）。

SDMC2 专有的三处处理：

1. `aic_sdmc2_power_on()` 在探测最早阶段按 `BSP_SDMC2_POWER_GPIO`
   （默认 `PD.1`）拉低 10 ms 再拉高，等待
   `BSP_SDMC2_POWER_DELAY_MS`（默认 50 ms），再按
   `BSP_SDMC2_BT_POWER_GPIO`（默认 `PD.3`）拉高 BT 复位；
2. `host.host.is_sdio = 1` 在任何时钟使能之前设置，使
   `hal_sdmc_clk_enable()` 永不置 `CLKCTRL.LOW_PWR`，与原厂
   `AIC_SDMC2_IS_SDIO=y` 行为一致；
3. 引脚只在 `BSP_USING_SDMC2` 时由 `board/pinmux.c` 配置为功能 2。

数据通路与 SDMC1 完全相同：命令/数据完成轮询，IDMA 优先、
`BSP_SDMC_USE_IDMA=n` 时走 CPU FIFO，块大小不超过 512 B；唯一的
中断是 `INTEN.FROM_SDIO`，供 `sdio_attach_irq()` 使用。

诊断接口：

- MSH `sdmc_dump [1|2]` 打印一个实例的时钟树、传输引擎和全部关键
  寄存器（`drv_sdmc.h` 导出 `aic_sdmc_dump()`）；
- Kconfig `BSP_SDMC_TRACE`（默认关）逐条打印 CMD52/CMD53 的参数与
  R5 数据字节。

### 12.15.3 配置

`configs/d213-sdio-utest.config` 的 SDMC 段：

```
# CONFIG_BSP_USING_SDMC1 is not set
CONFIG_BSP_USING_SDMC2=y
CONFIG_BSP_SDMC_USE_IDMA=y
CONFIG_BSP_SDMC2_POWER_GPIO="PD.1"
CONFIG_BSP_SDMC2_POWER_DELAY_MS=50
CONFIG_BSP_SDMC2_BT_POWER_GPIO="PD.3"
```

构建检查目标 `d213-sdio-utest`（`bsp/artinchip/tools/build-check.py`）
要求 ELF 中存在 `sdio_attach_irq`、`sdio_io_rw_extended`、
`hal_sdmc_idma_prepare`，字符串 `bsp.d213.sdio`、`SDIO IRQ armable`、
`WL_REG_ON`，并打包镜像。

### 12.15.4 实机结果（2026-11-08，`bsp.d213.sdio`）

HIL 产物：`bsp/artinchip/artifacts/d213-hil/20261108-052737-5140`。

| 项目 | 结果 | 证据 |
|---|---|---|
| 探测与上电 | PASS | `sdmc2: driver loaded (4-bit, IDMA polling, SDIO IRQ armable)` |
| CMD5/CMD3/CMD7 枚举 | PASS | 识别 `manf 0x024c product 0xb733`，1 个功能 |
| CCCR / FBR / CIS 指针 | PASS | CCCR rev `0x32`、caps `0x13`、CIS `0x001000`、FBR1 std `0x07` |
| 功能 1 使能 | PASS | `IO_EN=0x02`、`IO_RDY=0x02` |
| 块大小读写回读 | PASS | 写 512 → 读回 512 |
| SDIO 带内中断 | PASS | `sdio_attach_irq()` 后 `CCCR INT_EN=0x03`，`detach` 后回 `0x00` |
| CMD52 读/写 | PASS | 50 MHz / 25 MHz / 400 kHz 均正常，失败后仍可继续访问 |
| **CMD53 数据阶段** | **FAIL** | 全部 2^24 个卡时钟后 `DRTO`（状态 `0x20c`）；写方向 `EBE`（`0x801c`） |

CMD53 失败的覆盖范围：功能 0 CIS、功能 1 `0x0`/`0x10000`、1 B/64 B/
4×512 B、读与写、FIFO 与 IDMA、4-bit 与 1-bit、50 MHz 与 25 MHz——
**没有一次数据阶段开始过**（DAT 线上连起始位都没有）。

主机侧证据表明问题不在时钟/相位/引脚/寄存器编程：

```
sdmc2: clk 50000000 Hz, sclk 50000000 Hz, clk source 100000000 Hz, 4-bit, idma 1
sdmc2: clkctrl 00000001 dlyctrl b0000000 hctrl1 00000008 hctrl2 10000000
sdmc2: pbuscfg 00000200 inten 00000000 oinst 00000000 idmacap 00000000
sdmc2: ctc 02000001 fifocfg 20070008 ttmc ffffffff ctrst 0001a106
tc_sdio: PF.0..PF.5 PIN_CFG 00000332   (功能 2、驱动 3、上拉，无残留高位)
```

- `CLKCTRL=0x1` 表示已使能且未进 LOW_PWR；`DLYCTRL=0xB0000000`
  即原厂 `AIC_SDMC2_DRV_PHASE=3 / SMP_PHASE=0`；
- `CTRST` 的 `DATA_3_STATUS` 为 1，说明 DAT3 经焊盘到达控制器；
- CMD 编码 `0x20002375`（START|HOLD|PRV_DAT_WAIT|DAT_EXP|RESP_CRC|
  RESP_EXP|CMD53）与 CMD53 参数（功能/块模式/自增/地址/长度）逐位
  复核正确；
- `hal_sdmc_set_buswidth/set_ddrmode/set_blk/idma_prepare`、`hal_sdmc_init`、
  FIFOTH/CTC/TTMC 与原厂 HAL 逐函数比对为空差异；
- 同一块板子烧原厂镜像时该模组 Wi-Fi/蓝牙均正常（用户确认），说明
  硬件通路本身可用。

因此当前判断：模组接受了 CMD53 但**不参与数据阶段**，差异在尚未复现
的原厂行为或模组状态，而不在已核对的主机寄存器编程。

### 12.15.5 补充实验：首启时机、I/O Abort 与原厂镜像（2026-11-08）

为排除“模组上电稳定时间”和“失败后未中止卡端传输态”两个假设，同日
又做了三组实验。

**首启时机。** 用 `wdt_test start 2` 让板子看门狗复位，串口脚本在新一次
启动出现 `msh` 提示符后立即执行 `utest_run -thread bsp.d213.sdio`。
测试用例在 `t=1648 ms` 发起第一次 CMD53（功能 1、寄存器 0、4 字节、
字节模式、自增），仍在 368 ms 后以 `status 0x20c`
（`DRTO|DAT_DONE|CMD_DONE`）、`rsp 0x00001000`（R5 的 I/O 状态为 CMD，
无错误标志）失败，与启动 17 s 后的结果逐位相同。这说明失败与模组上电
后的等待时间无关。

**I/O Abort。** 驱动新增 `aic_sdmc_card_abort()`：CMD53 数据错误时，
先向 CCCR `0x06` 写入功能号（带控制器 `STOP` 标志）请求中止该功能，
再软复位控制器；若第一次没发出去，复位后再试一次。实机日志每次都打印
`CMD52 abort function 1 ret 0`，但紧接着重复同一条 4 字节 CMD53 仍以
`0x20c` 失败，第二次的 R5 状态为 `0x2000`（TRANS），即功能 1 仍停在
传输态。结论是卡完成了 CMD52（无 RTO/CRC 错误），但中止没有恢复数据
通路；需要说明的是，当前实现只检查命令完成，没有解码 R5 的
`ILLEGAL_COMMAND`/`ERROR`/`OUT_OF_RANGE` 位，因此“卡是否真正接受中止”
还不能下最终结论。

**原厂镜像对照。** 烧写原厂 Luban-Lite 镜像
（`demo128_nand_carplay_page_2k_block_128k`，8 MB os / 32 MB data）后，
SDMC2 同样以 400 kHz 完成枚举再切到 50 MHz、4-bit，出现与本 BSP 完全
相同的两条低电压 OCR 警告，最后打印
`sdio_mgr: SDIO Device initialization complete`；仅从枚举看，两边没有
差别。原厂 Realtek 自带命令 `wlan wifi_on` 并不走 rt-wlan 框架，
`realtek_init()` 未被调用，闭源库取不到 SDIO function
（`[gspi_dvobj_init] Oops: get wifi sdio function fail`）并触发 CPU
异常，不能用来验证数据通路。正确的原厂触发路径是框架命令 `wifi`
（例如 `wifi scan`）：它打开 wlan 设备后经 `aic_realtek_init()` →
`realtek_init()` → SDIO probe 进入 Realtek 驱动。该实验待执行。

**400 kHz 的边界。** 1-bit / 400 kHz 的 64 字节读在 1015 ms 返回
`-ETIMEOUT`，是主机 1 s 轮询上限先到，而不是卡的 2^24 卡时钟超时
（400 kHz 下约 42 s）。这一档仍不能作为“400 kHz 也不工作”的严格结论。

（后续按要求停止原厂固件工作，`wifi scan` 对照实验取消；原厂侧的
验证改为对镜像中闭源库做静态反汇编，见 12.15.6。）

### 12.15.6 原厂初始化序列实验（2026-11-08）

12.15.5 的分析停在“差异在尚未复现的原厂行为”。这一步沿着原厂闭源库的
反汇编把范围缩小到了一个具体序列，并在实机上验证。

**实验内容。** 原厂 `init_sdio_cfg_8723f` 在数据阶段之前对该功能做两个
读改写（HALMAC 地址 `0x10250000` / `0x10250085`，经
`cnv_to_sdio_bus_offset_87xx` 转换为功能 1 本地寄存器 `0x000` / `0x085`）：

- 32 位本地寄存器 `0x00` 读出后写回 `value & 0xDFF8`；
- 8 位本地寄存器 `0x85` 读出后写回 `value | 0x02`。

经用户授权，测试用例临时按原厂序列执行了这两个读改写（模块每次启动由
`WL_REG_ON` 复位，写入不持久），并在写入前后读取
`0x00`/`0x85`/`0x100`/`0x101`。HIL 产物：
`bsp/artinchip/artifacts/d213-hil/20261108-174515-12872`。

```
tc_sdio: vendor init: local 0x00 32 00 00 0c -> 30 00 00 00 (write 0 0 0 0)
tc_sdio: vendor init: local 0x85 01 -> 03 (write 0), local 0x100 ea, 0x101 ea, ...
tc_sdio: fn1 4-byte read ret -5 (695 ms) ...      status 0x20c, rsp 0x00001000
```

结论分两层：

- 模组接受这两个写：四次字节写全部返回 0，回读 `0x00=30 00 00 00`、
  `0x85=0x03`，与掩码一致；重启后 `0x85` 又回到 `0x01`，确认模组被
  复位、写入不持久；
- **CMD53 数据阶段仍不启动**：首次 4 字节读在 25 MHz / 1-bit 下依旧
  694 ms 后 `DRTO`（`0x20c`），块模式、字节模式、功能 0 CIS、功能 1
  `0x10000`、写方向等全部与实验前逐位相同。

**闭源库反汇编给出的定位。** 同一天对原厂镜像中的 Realtek 闭源库做了
进一步反汇编，得到三条关键事实：

1. `mac_pwr_switch_sdio_8723f()` 读本地寄存器 `0x100`，上电分支执行
   `card_en_flow_8723f` 上电序列表之后，把适配器内部标志（`+536`）
   置 1；该标志为 1 时，原厂所有寄存器访问改走 **CMD53 间接通路**
   `r_indir_cmd53_87xx()`（对本地寄存器 `0x42` 做 8 字节 CMD53 读）。
   即原厂在上电序列结束后立刻依赖 CMD53 工作。
2. HALMAC 初始化顺序是 `rtw_hal_power_on()` → `download_fw()` →
   `init_mac_flow()`；固件下载经 `rtl8723fs_write_port()` →
   `rtw_sdio_write_cmd53()`，**本身就用 CMD53**。
3. 本次复现的 `init_sdio_cfg_8723f` 属于 `init_mac_flow()`，发生在
   固件下载之后，因而不是数据通路的开关。

也就是说，模组的 SDIO 数据通路由原厂**芯片上电序列**打开，而不是由
主机侧寄存器编程或这两个读改写打开。原厂 SDIO 主机栈（RT-Thread
`components/drivers/sdio`）与 AIC SDMC HAL 已逐函数比对，除
`AIC_SDMC_DRV` 下一个只影响大于 512 字节块传输的 `max_blks` 钳位外
没有差异；主机侧的 CMD53 命令/参数/寄存器构造也已逐位核对。

**测试用例的调整。** 实验目的完成后，用例恢复为**只读**探测（不再写
功能 1 的 I/O 寄存器），保留本地寄存器 `0x00`/`0x85`/`0x100` 的
只读打印。断言改为：若数据阶段真的启动，则继续断言块模式；否则断言
失败的数据阶段后功能 1 仍可经 CMD52 访问。实机
`bsp.d213.sdio` 现已通过（HIL 产物
`bsp/artinchip/artifacts/d213-hil/20261108-180448-15208`）。

### 12.15.7 结论与边界

- 可以确认：SDMC2 SDIO 主机路径已接入标准 RT-Thread SDIO 栈，
  枚举、CCCR/FBR/CIS、CMD52、功能使能、块大小、带内中断均已实机通过；
- 可以确认：CMD53 数据阶段未启动的原因是模组侧数据通路由原厂上电
  序列打开（12.15.6），主机命令/参数/寄存器构造与原厂一致；
- **不能确认**：任何 CMD53 数据收发、吞吐、中断时延，进而不能声称
  该模组的 Wi-Fi/蓝牙可用；
- 原厂镜像能完成同样的 SDIO 枚举，但其 CMD53 数据阶段日志未取得：
  `wlan wifi_on` 绕过 rt-wlan 框架并导致闭源库崩溃，而框架命令
  `wifi scan` 的对照实验因已停止原厂固件工作而取消；
- 测试用例保持只读：只写 CCCR/FBR 控制寄存器，**不写功能 1 的
  I/O 寄存器**（12.15.6 的一次性原厂序列实验已撤销）；
- 模组固件、Realtek 驱动、WLAN/BT 协议栈不在本 BSP 范围内。

### 12.15.8 已排除项与下一步

已排除（均有实机/寄存器证据）：时钟频率与分频、采样/驱动相位、
4-bit 与 1-bit、FIFO 与 IDMA、功能/地址/长度/读写方向、`is_sdio` 与
`LOW_PWR`、引脚功能/上拉/驱动强度、PD.3 未拉高、CTC/FIFOTH/TTMC/
PBUSCFG 配置、HAL 寄存器编程差异、模组上电后的等待/空闲时间
（`t=1648 ms` 与启动 17 s 后结果逐位相同）。

已尝试但未生效：CMD53 失败后先向 CCCR `0x06` 发 I/O Abort 再复位
控制器（CMD52 每次完成，重复 CMD53 仍以 `0x20c` 失败）；按原厂
`init_sdio_cfg_8723f` 写入功能 1 本地寄存器 `0x00`/`0x85`（写入被
卡接受，数据阶段不变，12.15.6）。`aic_sdmc_card_abort()` 已实现
R5 解码，实测中止请求每次返回状态 1（CMD）→ 2（TRANS）且无
`ILLEGAL_COMMAND`/`ERROR`/`OUT_OF_RANGE` 标志，中止本身是成功的。

下一步按顺序验证：

1. 复现原厂 IRQ 模式数据通路：`HCTRL1.INT_EN=1` + `INTEN=DAT_DONE`，
   ISR 清 `DAT_DONE` 并唤醒，数据仍走 IDMA；
2. 把 1-bit / 400 kHz 首次数据尝试的主机轮询上限放宽到超过 2^24
   卡时钟（约 42 s），排除高速与位宽残留状态的影响；
3. 若确实需要该模组的 Wi-Fi/蓝牙，则必须移植原厂无线驱动栈
   （闭源 `wlan_lib` + HALMAC 上电/固件下载序列，命令与数据通路
   见 12.15.6）。这涉及闭源依赖策略，属于独立任务，需另行确认；
   在该栈运行之前，CMD53 数据阶段不可用是模组侧的设计属性。
