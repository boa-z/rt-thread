# 12 存储系统：QSPI、SPI NAND、MTD 与文件系统

## 12.1 当前状态

**未实现（N/I）。**

当前 BSP 不使用 SPI NAND 作为 RT-Thread 存储设备。镜像中的 OS 分区由 ArtInChip Bootloader 读取和加载，RT-Thread 运行时不挂载 NAND、MTD 或文件系统。

这意味着：

- 没有 `mtd` 设备；
- 没有 `dfs`/FatFs/LittleFS；
- 没有 NAND 坏块管理；
- 没有 ECC 状态上报；
- 没有可写的持久化配置区；
- 没有 OTA 分区逻辑。

不要把“Bootloader 能从 NAND 加载镜像”误解为“RT-Thread 已支持 NAND”。

## 12.2 硬件资源

D213 demo128：

| 项目 | 参数 |
|---|---|
| NAND | 128 MB SPI NAND |
| 控制器 | QSPI0 |
| Page | 2 KB |
| Block | 128 KB |
| 启动 | BROM → SPL → Bootloader → OS |
| OS 分区 | 原厂 `image_cfg.json` 定义（约 4 MB） |
| 备份 | 原厂分区表中存在 env/env_r 等备份区域 |

原厂分区表包含 `spl`、`env`、`env_r`、`os` 等区域。存在备份区不等于自动具备 A/B OTA 或故障回滚；回滚逻辑需要单独实现和验证。

## 12.3 为什么没有直接复制原厂 NAND 驱动

Luban-Lite 的 NAND 路径涉及：

```text
hal/qspi/            QSPI 控制器
hal/dma/             DMA（部分路径）
drv/spinand/         SPI NAND 设备、BBM、ECC
drv_bare/spinand/    MTD 适配
drv_bare/mtd/        MTD core/parts
drivers/mtd/         RT-Thread MTD
DFS/FatFs/LittleFS   文件系统
```

依赖链包括：

- QSPI 控制器版本（v1.0/v1.1/v3.x）；
- DMA 通道和 Cache 一致性；
- NAND 芯片 ID/参数表；
- ECC 引擎；
- 坏块表；
- 分区解析；
- 原厂 FTL/NFTL（如使用）；
- PM/复位/时钟。

在当前没有串口和实机条件的情况下，直接复制会带来高风险：

1. 错误的 ECC/BBM 可能把坏块当做好块；
2. 错误的 QSPI 时序可能导致读写出错；
3. 错误的擦除地址可能破坏 Bootloader 或 env；
4. 没有硬件验证就无法证明数据可靠性。

因此本轮明确不把“能编译的 NAND 驱动”冒充“可靠存储”。

## 12.4 安全的后续接入顺序

建议按以下顺序实施，每一步都必须有实机证据：

### 阶段 1：只读探测

- 接入 QSPI0 控制器 HAL；
- 读取 JEDEC ID；
- 读取 NAND 参数页；
- 只做读，不擦写；
- 用逻辑分析仪确认时序。

### 阶段 2：MTD 只读

- 注册 MTD 设备；
- 读取分区表；
- 对比 Bootloader 读出的镜像 CRC；
- 不挂载可写文件系统。

### 阶段 3：受控擦写

- 在**非启动分区**（例如 data 分区）做擦除/写入；
- 先备份、再验证；
- 每次操作打印地址、长度、ECC 状态；
- 断电测试。

### 阶段 4：文件系统

- LittleFS（掉电恢复较好）或 FatFs（兼容性较好）；
- 先在 RAM 块设备上验证；
- 再切换到 MTD；
- 做 1000 次掉电/写入循环。

### 阶段 5：OTA

- 明确 A/B 分区；
- 校验签名/CRC；
- 回滚策略；
- 不能只依赖“有 env_r 备份”就声称支持 OTA。

## 12.5 高风险操作清单

以下操作在未确认前**禁止**执行：

- 擦除 `spl`、`bootloader`、`env`、`os` 分区；
- 写入 Bootloader 地址 `0x47B80000`；
- 修改 `image_cfg.json` 的分区偏移；
- 在 RT-Thread 中直接对 `0x0` 起始的 NAND 做全片擦除；
- 关闭 ECC；
- 忽略坏块标记；
- 把 NAND 当 NOR 直接按字节写。

## 12.6 文件系统选择

| 方案 | 优点 | 风险 |
|---|---|---|
| LittleFS | 掉电恢复、磨损均衡、适合小分区 | 需要 block device + 正确 erase/program |
| FatFs | 生态成熟、PC 可读 | 掉电时 FAT 表可能损坏 |
| 原厂 FTL/NFTL | 与 Bootloader 兼容 | 依赖复杂、许可证/维护成本 |
| 裸 MTD + 自定义 | 可控 | 需要自己实现可靠性 |

本 BSP 暂不预设文件系统。接入时必须先明确介质、分区、掉电要求和 PC 互操作需求。

## 12.7 对 Bootloader/镜像的约束

- OS 镜像必须仍然放在原厂 `os` 分区；
- 镜像大小不能超过分区；
- FIT 加载地址/入口不能改；
- 不要修改 DDR 参数；
- 不要覆盖 Bootloader 保留区（堆结束 `0x47800000`）；
- 任何分区表修改都必须同时更新 `pack/image_cfg.json` 和打包脚本，并做回滚测试。

## 12.8 验证状态

| 项目 | 状态 |
|---|---|
| QSPI 控制器 | N/I |
| SPI NAND 识别 | N/I |
| ECC | N/I |
| 坏块管理 | N/I |
| MTD | N/I |
| 分区访问 | N/I |
| 文件系统 | N/I |
| 掉电恢复 | N/I |
| OTA | N/I |

在完成上述阶段前，本 BSP 不提供存储相关的 MSH 命令，也不在文档中给出“可用”的挂载示例。
