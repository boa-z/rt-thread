# 22 D213 utest 回归体系

## 22.1 目标和边界

本测试固件用于把 D213 BSP 从“能启动、能手工敲命令”推进到可重复的内核与基础外设回归。

约束：

- 只修改 `bsp/artinchip/d213ecv-demo-v4-0/` 和 `bsp/artinchip/tools/`。
- 不修改 `libcpu/risc-v/common/`。FPU 上下文修复来自官方 master，不再维护独立修复分支，失败按普通回归处理。
- 默认构建不启用 utest。`d213-minimal` 和 `d213-default` 的行为、设备注册顺序和 UART0 轮询 RX 路径保持不变。
- UART0 属于控制台。测试只验证控制台已打开和可写，不测试会破坏控制台配置的波特率切换或 RX 中断切换。
- WDT 复位、破坏性内存测试不进入自动回归；NAND 擦写只允许在未分配
  尾部 `nand_scratch`（当前物理块 916~1023）上执行，声明分区始终只读。

## 22.2 构建配置

新增配置：

```text
configs/d213-utest.config
```

它在 `d213-default` 的基础上增加：

- `CONFIG_BSP_USING_UTEST=y`
- `CONFIG_BSP_UTEST_CAN_LOOPBACK` 默认关闭，只有外部 TX/RX 回环就绪时才启用
- `CONFIG_RT_USING_UTEST=y`
- `CONFIG_RT_USING_UTESTCASES=y`
- `CONFIG_BSP_USING_GPAI6=y`、`CONFIG_BSP_USING_KEYADC=y`，供
  `bsp.d213.keyadc` 检查 ADC 设备并把梯形电阻码值映射到 K4-K7
- `CONFIG_RT_UTEST_SELF_PASS=y`，接入官方 `TC_uassert.c`
- `CONFIG_RT_UTEST_IRQ/SEMAPHORE/MUTEX=y`
- `CONFIG_RT_CONSOLEBUF_SIZE=256`，utest 框架要求不小于 256
- `CONFIG_FINSH_USING_OPTION_COMPLETION=y`，utest 源文件无条件引用 `RT_UTEST_MAX_OPTIONS`
- `CONFIG_RT_USING_HOOK/y`、`CONFIG_RT_HOOK_USING_FUNC_PTR=y`，官方 `core.irq` 需要中断 hook
- DFS v1（`RT_USING_DFS`、`RT_USING_DFS_V1`、`DFS_USING_POSIX`、
  `DFS_USING_WORKDIR`）+ ELM FAT + ROMFS + ROMFS_USER_ROOT，供
  `/rodata` 只读挂载
- `CONFIG_BSP_SPINAND_RODATA_FS=y`、`CONFIG_BSP_SPINAND_LITTLEFS=y`、
  `CONFIG_BSP_SPINAND_LITTLEFS_AUTOFORMAT=y`、
  `CONFIG_BSP_SPINAND_LITTLEFS_SPARE_BLOCKS=4`

测试构建不要使用 `--useconfig` 后直接编译。推荐使用构建矩阵脚本：

```sh
python bsp/artinchip/tools/build-check.py \
    --targets d213-utest \
    --sdk /path/to/luban-lite
```

脚本会恢复原有 `.config`/`rtconfig.h`，测试固件产物在：

```text
artifacts/d213-utest/
```

也可以手工切换：

```sh
cd bsp/artinchip/d213ecv-demo-v4-0
cp configs/d213-utest.config .config
scons --pyconfig-silent
scons -j8
```

不要把 `.config` 切换后的临时结果提交为默认配置。

## 22.3 测试用例组织

```text
bsp/artinchip/d213ecv-demo-v4-0/
├── utest/
│   ├── SConscript
│   ├── context_gcc.S        # s0..s11 上下文标记和校验
│   ├── tc_smoke.c
│   ├── tc_tick.c
│   ├── tc_scheduler.c
│   ├── tc_context.c
│   ├── tc_fpu.c
│   ├── tc_uart.c
│   ├── tc_gpio.c
│   ├── tc_i2c.c
│   ├── tc_rtc.c
│   ├── tc_tsen.c
│   ├── tc_ce.c             # 仅 BSP_USING_CE
│   ├── tc_pwm.c
│   ├── tc_can.c
│   ├── tc_display.c
│   ├── tc_dmac.c            # 仅 BSP_USING_DMAC
│   ├── tc_uart_dma.c        # 仅 BSP_USING_UART1 && BSP_USING_UART_DMA
│   ├── tc_sdcard.c          # 仅 BSP_USING_SDMC1
│   ├── tc_uart7.c           # 仅 BSP_USING_UART7
│   ├── tc_sdio.c            # 仅 BSP_USING_SDMC2
│   ├── tc_audio.c           # 仅 BSP_USING_AUDIO
│   ├── tc_dmic.c            # 仅 BSP_USING_DMIC
│   ├── tc_nand_write.c      # 仅 BSP_SPINAND_SCRATCH_MTD
│   ├── tc_nand_fs.c         # 仅 BSP_SPINAND_RODATA_FS && BSP_SPINAND_LITTLEFS
│   ├── tc_usb.c             # 仅 BSP_USING_USB0_HOST/BSP_USING_USB1_HOST
│   ├── tc_usbdev.c          # 仅 BSP_USING_USB0_DEVICE
│   └── tc_eth.c             # 仅 BSP_USING_GMAC0
├── applications/
│   ├── main.c
│   ├── selftest.c           # aic_selftest 和共享 FPU 上下文回归
│   └── fpu_test_gcc.S
└── configs/
    ├── d213-utest.config
    ├── d213-sdcard-utest.config
    ├── d213-sdio-utest.config
    ├── d213-audio-utest.config
    ├── d213-usb-utest.config
    ├── d213-usbdev-utest.config
    ├── d213-ce-utest.config
    ├── d213-eth-utest.config
    ├── d213-dma-utest.config
    └── d213-uart-dma-utest.config
```

`utest/SConscript` 只在 `BSP_USING_UTEST=y` 时编译 `tc_*.c` 和上下文汇编。默认构建不会把测试代码链接进镜像。

### P0 内核回归

| 用例 | 覆盖内容 | 清理行为 |
|---|---|---|
| `components.utilities.utest.uassert` | 官方 utest 断言接入 | 无全局资源 |
| `bsp.d213.smoke` | utest 注册、线程句柄、堆分配 | 释放临时堆块 |
| `bsp.d213.tick` | tick 递增、`rt_thread_mdelay()`、单次软件定时器 | 删除定时器、detach 信号量 |
| `bsp.d213.scheduler` | 线程创建、退出、优先级切换、互斥量、信号量超时 | 删除工作线程、detach IPC |
| `bsp.d213.context` | 整数 callee-saved 寄存器跨切换保持 | 阻塞等待有超时 |
| `bsp.d213.fpu.context` | `fs0..fs11` 跨线程切换保持 | 双线程握手，所有等待有上限 |
| `bsp.d213.uart0` | 控制台设备已打开、TX 可写 | 不改变控制台配置 |

### P1 基础外设

| 用例 | 覆盖内容 | 硬件条件 |
|---|---|---|
| `bsp.d213.gpio` | GPIO 设备注册、`PA.0`/`PG.31` 名称解析、非法名称拒绝 | 无 |
| `bsp.d213.i2c3` | I2C3 总线访问 GT911 产品 ID | GT911 在 I2C3 上 |
| `bsp.d213.rtc` | RTC 连续读取并递增；`RT_USING_ALARM` 时再验证 3 s 后一次性闹钟：回调只触发一次、时间戳不早于目标、触发后硬件闹钟已关闭 | 32 kHz 晶振/电池 |
| `bsp.d213.tsen` | 查找 `temp_tsen_cpu`、打开传感器、查询 info（温度类/0.1 °C）、连续 3 次读取，返回值类型正确且落在 -40..125 °C 内，打印每次温度 | 无；需要 `BSP_USING_TSEN` 和 `BSP_USING_TSEN_CPU`，`configs/d213-utest.config` 已开启 |
| `bsp.d213.keyadc` | 查找 `gpai` 设备、查询 12 位分辨率和 2.8–3.15 V 参考电压；用 1.0/2.2/4.7/6.8 k 梯形计算期望码值（2800/3000/3150 mV 三档），验证每个键在 ±150 码内可识别、相邻键中点与释放满量程返回 `AIC_KEY_NONE` | 无外部设备；需要 `BSP_USING_GPAI6` 和 `BSP_USING_KEYADC`，`configs/d213-utest.config` 已开启。只验证解码和 ADC 设备注册，不读取通道，避免与 `keyadc` 采样线程竞争 |
| `bsp.d213.ce` | hwcrypto 默认设备注册；SHA-256 单次/分段更新（`abc`、56 B 两段）、SHA-1、MD5、AES-128/256 ECB 加解密、AES-128 CBC 加解密，全部与 FIPS-197/NIST/RFC 已知答案比对；另有 64 B 对齐 ECB 用例走驱动直通 DMA 路径；创建上下文期间 `CLK_CE` 已使能 | 无外部设备；需要 `BSP_USING_CE`，用 `d213-ce-utest` 构建。只证明 CE 与框架通路，不覆盖 DES/SM2/SM4/HMAC/TRNG/RSA 或性能 |
| `bsp.d213.pwm` | PWM0 设置、使能、关闭 | 不影响 PC.7 面板使能 |
| `bsp.d213.can0.config` | CAN0 打开、500 kbit/s、normal 模式、状态读取 | 无 |
| `bsp.d213.can0.loopback` | CAN0 SELFTEST 发送并接收一帧 | 需要外部 TX/RX 回环，或收发器+第二节点；默认不编译 |
| `bsp.d213.spinand.ro` | JEDEC ID/几何、`os` 页 0 重复读 100 次、越界拒绝、坏块标记、写/擦/移动/标坏必须返回 `-RT_EPERM` | D213 板载 SPI NAND；无 NAND 时打印跳过信息，不误报通过 |
| `bsp.d213.spinand.rw` | 声明分区写/擦/移动/标坏必须返回 `-RT_EPERM`；`nand_scratch` 上整块擦除逐页校验、单页编程回读、邻页不扰动、`move_page` 回读、再擦除、`mark_badblock` 后 `check_block` 为坏块且擦除后恢复好块 | D213 板载 SPI NAND + `BSP_SPINAND_SCRATCH_MTD=y`；只在未分配尾部 916~1023 写入，测试结束保持擦除态 |
| `bsp.d213.spinand.fs` | `/rodata` 只读 FAT：statfs、块设备 GETGEOME、扇区 0 的 `0x55AA`/`FAT` 标识、写和擦除拒绝；`/nand`：statfs、BBT 一致性、目录创建、4 KiB 写读比对、stat/rename/unlink、目录清空删除、持久化计数递增 | D213 板载 SPI NAND + `BSP_SPINAND_RODATA_FS=y` + `BSP_SPINAND_LITTLEFS=y`；只写 `nand_scratch`，不触碰声明分区 |
| `bsp.d213.ota` | 只读解析 `env`/`env_r`（CRC32、FLAG_INCREMENTAL、活动副本）、A/B 键（`osAB_now`/`osAB_next`/`upgrade_available`/`bootlimit`/`bootcount`）以及 `os`/`os_r` FIT 头；不写任何分区 | D213 板载 SPI NAND + `BSP_USING_SPINAND_MTD_NAND`；`os` 必须有效，`os_r` 允许为空 |
| `bsp.d213.sdcard` | 等待 `sd0`、`RT_DEVICE_CTRL_BLK_GETGEOME`、512 B 扇区、64 扇区重复读一致、扇区 0 的 `0x55AA` 签名 | SD 卡插入 SDMC1；无卡时打印 SKIP，不误报通过 |
| `bsp.d213.uart7` | UART7（BT HCI）打开、默认 115200 8N1、MCR 内部回环收发 32 字节逐字节校验；测试期间临时关闭自动流控 | 无外部连线；需要 `BSP_USING_UART7`；RTS/CTS 本身需模组对端 |
| `bsp.d213.sdio` | 模组识别（厂商/产品 ID）、CCCR/FBR/CIS、功能 1 使能与 IO_RDY、块大小读写回读、CMD52、CMD53 块/字节读、带内中断 attach/detach；失败时打印 `sdmc_dump` 与焊盘 PIN_CFG | RTL8733BS 模组接 SDMC2；无模组时 SKIP，不误报通过 |
| `bsp.d213.audio` | `sound0` 注册、打开、capability 查询、48 kHz/2 声道/16-bit 配置、非法采样率拒绝、音量设置、写入 8 KiB 后 DMA 周期计数推进、关闭排空 | 无外部设备；需要 `BSP_USING_AUDIO` 和可用 DMAC 通道。只证明时钟/DRQ/DMA 通路，不证明声学输出 |
| `bsp.d213.dmic` | `dmic0` 注册、只读打开、capability 查询（INPUT/MIXER）、96 kHz 拒绝、16 kHz 单/双声道切换、音量设置、DMA 周期计数推进、读取 2 KiB 采集数据、关闭 | 无外部设备；需要 `BSP_USING_DMIC` 和可用 DMAC 通道。只证明 PLL/抽取/DRQ/DMA 通路，不证明声学输入 |
| `bsp.d213.usb` | 等待 `sda` 注册（MSC 枚举 + FAT 自动挂载）、`RT_DEVICE_CTRL_BLK_GETGEOME`、512 B 扇区、两次 8 扇区读一致、扇区 0 的 `0x55AA` 签名 | USB1 Host 接 U 盘（`BSP_USING_USB1_HOST`）；无 U 盘时用例 FAIL，不静默跳过 |
| `bsp.d213.usbdev` | `usb-acm0` 字符设备已注册且类型为 `RT_Device_Class_Char`；UDC 时钟 `CLK_USBD`/`CLK_USB_PHY0` 已使能，`RESET_USBD`/`RESET_USBPHY0` 已释放 | 不需要 USB 线；需要 `BSP_USING_USB0_DEVICE`，用 `d213-usbdev-utest` 构建。套件只证明设备栈/chardev 初始化；枚举、CDC 双向数据和 `usb_dev_console on/off` 切换已另行实机验证（16 轮，`20261109-053348`） |
| `bsp.d213.eth` | 等待 `e0` 链路 UP（10 s）、DHCP 租约（20 s）、IP/掩码非 0；打印地址。链路或 DHCP 未就绪时 FAIL | 网线接路由器（`BSP_USING_GMAC0`）；无链路时用例 FAIL，不静默跳过 |
| `bsp.d213.dmac` | 4 KiB/64 KiB 内存到内存复制逐字节校验、4 B 长度拒绝、连续 8 次 8 KiB；8 通道全部占用后第 9 次申请失败、释放后可再申请；200 次申请/释放后描述符池仍完整；循环传输参数检查（period 未对齐、buf 非整周期、周期数超过描述符池）拒绝且不留残链 | 无；需要 `BSP_USING_DMAC` 并按 `d213-dma-utest` 构建 |
| `bsp.d213.uart1.dma` | UART1 经外部 DMAC 发送，MCR 内部回环收回：64 B/1 KiB/4 KiB 逐字节校验、4 B 拒绝；8 个 513 B 分片连续排队（奇数长度使分片起始不对齐）、8 KiB/1 KiB 对齐分片；8 通道全被占用时的轮询回退；TX DMA 与标准 `INT_RX` ring 并发；传输中关闭设备后重开仍可发送 | 无连线；需要 `BSP_USING_UART_DMA` 并按 `d213-uart-dma-utest` 构建 |

`bsp.d213.spinand.ro` 需要 `BSP_USING_SPINAND_MTD_NAND`；当前
`configs/d213-utest.config` 已开启。该用例全程只读，不会产生
Program/Erase/Refresh 命令。

`bsp.d213.spinand.rw` 需要 `BSP_USING_SPINAND_MTD_NAND` 且
`BSP_SPINAND_SCRATCH_MTD=y`（`configs/d213-utest.config` 已开启）。
它只对驱动注册的 `nand_scratch`（当前物理块 916~1023）执行
擦/写/移动/标坏，并在结束前恢复为擦除态；声明分区保持只读。
2026-11-08/09 实机：HIL `20261108-225516-34668`（`.ro` + `.rw` 均
PASS）、`20261108-225807-31876`（加入标坏用例后 `.rw` PASS），
源码定稿后 clean rebuild 复核 `20261108-230606-16252`（`.ro` +
`.rw` PASS）。

`bsp.d213.spinand.fs` 需要 `BSP_SPINAND_RODATA_FS=y` 和
`BSP_SPINAND_LITTLEFS=y`（`configs/d213-utest.config` 已开启），
并使用 DFS v1。它先验证 `/rodata` 的只读 FAT（FAT12 BPB、`0x55AA`、
写/擦拒绝），再在 `/nand` 上创建目录和 4 KiB 文件，完成写读比对、
stat、重命名和删除，最后读取并递增 `/nand/persist.bin`。2026-11-09
实机：HIL `20261109-000938-77752` PASS，输出
`nand_scratch 108 blocks, 102 logical, 4 spare, 0 bad, BBT seq 1`、
`persistence boot=3`；同一固件再次运行得到 `boot=4`。最终全量回归
`20261109-002129-38512` 中该套件再次 PASS（`persistence boot=5`），
`bsp.d213.*` 15/16 通过，唯一失败为已知的 `bsp.d213.fpu.context`。
数据与 BBT 映射跨复位保持。详细设计见 `12-storage.md` 12.5.7~12.5.9。

2026-11-09 GE 中断修复与像素校验提交后的最终镜像（`f1d96e60e4`，
`d213-usbdev-utest`，USB1 Host 接 U 盘）全量回归 HIL
`20261109-065053-19440`：16 个用例 15 PASS，唯一失败为已知的
`bsp.d213.fpu.context`；`bsp.d213.usb` 在 U 盘插入后 PASS。

2026-11-09 第二阶段适配器下沉后（`35e9561906`）全量回归 HIL
`20261109-171642-13288`（`d213-utest` 镜像，flash scope `os`）：
17 个测试（22 个 testcase）16 PASS，唯一失败仍为已知的
`bsp.d213.fpu.context`，HIL 状态 `TEST_KNOWN_FAIL`；本轮新增的
`bsp.d213.ota` PASS（`osAB_now=A`、`os FIT totalsize 729`）。
同日 `d213-uart-dma-utest` 回归 HIL `20261109-171853-28020`：
`bsp.d213.uart1.dma` 9/9 PASS，整轮同样仅 FPU 已知失败。

`bsp.d213.ota` 需要 `BSP_USING_SPINAND_MTD_NAND`（`configs/d213-utest.config`
已开启），只调用 `drv_ota_slot.c` 和 `aic_nand_get_fit_info()` 的只读
接口。2026-11-09 实机：HIL `20261109-015923-59712`（`bsp.d213.ota`
+ `bsp.d213.spinand.fs`，flash scope `os`）PASS，输出
`osAB_now=A`、`osAB_next=A`、`os FIT totalsize 729`、`os_r is empty`；
同次运行的 `bsp.d213.spinand.fs` 输出 `persistence boot=6`。
2026-11-09 实机：`tools/d213_storage_soak.py --port COM9
--stress-cycles 2 --stress-seconds 30` 全流程 PASS（报告
`bsp/artinchip/artifacts/d213-storage-soak/20261109-035532`）。
`aic_ota_slot_status`/`aic_nand_partitions` 文本输出正确；BBT 从
seq 2/active 1 擦除 slot 1 后恢复为 seq 1/active 0；两轮 WDT 复位
分别发生在 stress seq 39/42，`/nand` 每次重新挂载，载荷校验通过。
该次执行暴露的 BBT 首次创建单副本缺陷已修复，见 `18-decisions.md`
ADR-025。

`bsp.d213.sdcard` 需要 `BSP_USING_SDMC1`，使用
`configs/d213-sdcard-utest.config`（关闭 PWM，打开 SDMC1）构建：

```sh
python bsp/artinchip/tools/build-check.py \
    --targets d213-sdcard-utest --sdk /path/to/luban-lite
```

该用例只读，不写卡。两个 32 KiB 比较缓冲必须为 `static`，否则超过
utest 线程默认 8 KiB 栈；64 扇区（32 KiB）在 IDMA 下跨越 8 个 4 KiB
描述符，同时验证多描述符链。SD 配置同时把
`CONFIG_RT_MMCSD_STACK_SIZE` 提到 8192，避免 MMCSD 检测线程在 RV64
上栈溢出。无卡时用例返回 PASS 但打印 SKIP，**不能**当作卡路径通过。

2026-11-07 D213 实机验证：`d213-sdcard-utest` 经 HIL OS-only 烧录后
`utest_run bsp.d213.sdcard` PASS（512 B 扇区、31116288 扇区，两次 8
扇区读内容一致）；`d213-sdcard` 固件把 14.856 GB FAT 分区挂载到 `/`，
`ls`/`df`/`cat` PASS。实机同时暴露了多块读缺少 CMD12 导致第二次
CMD18 报忙的问题，已在 `drv_sdmc.c` 的 `aic_sdmc_send_stop()` 中修复。
2026-11-08 复测（HIL `20261108-064817-14836`）：64 扇区与 `0x55AA`
签名用例再次 PASS。2026-11-09 复测（HIL `20261109-065314-25072`）：
`d213-sdcard-utest` 全流程 PASS，512 B 扇区、31116288 扇区。

`bsp.d213.sdio` 需要 `BSP_USING_SDMC2`，使用
`configs/d213-sdio-utest.config`（关闭 SDMC1，打开 SDMC2 和 SDIO 栈；
同一配置也打开 UART7 与 BT HCI 自动流控，因为二者属于同一个模组，
所以该镜像同时包含 `bsp.d213.uart7` 套件）构建，并用 HIL 全流程跑：

```sh
python bsp/artinchip/tools/build-check.py \
    --targets d213-sdio-utest --sdk /path/to/luban-lite
python bsp/artinchip/tools/d213_hil.py --mode full \
    --confirm-board d213-demo128 --target d213-sdio-utest \
    --suite bsp.d213.sdio --sdk /path/to/luban-lite
```

2026-11-08 实机结果：枚举、CCCR/FBR/CIS、功能 1 使能、块大小读写回读、
CMD52 读写、带内中断 attach/detach 全部 PASS；**CMD53 数据阶段在
所有功能/地址/长度/位宽/时钟/传输引擎组合下都以 `DRTO` 失败**，因此
该用例当前以 `TEST_FAIL` 结束。缺陷分析、已排除项和下一步见
`12-storage.md` 12.15。该用例只写 CCCR/FBR 控制寄存器，不写功能 1
的 I/O 内存。

`bsp.d213.uart7` 使用同一 `d213-sdio-utest` 镜像。2026-11-08 实机
（HIL `20261108-064913-21800`）：`tc_uart7_loopback` PASS——`uart7`
注册、默认 115200 8N1、MCR 内部回环 32 字节逐字节一致，测试后恢复
原 MCR。内部回环不经过模组，因此 RTS/CTS 仍需模组对端验证。

`bsp.d213.audio`（播放）和 `bsp.d213.dmic`（DMIC 采集）使用同一个
`configs/d213-audio-utest.config`（打开 RT-Thread audio、mempool、
DMAC 和 `BSP_USING_DMIC`，关闭 SDMC2/UART7）构建：

```sh
python bsp/artinchip/tools/build-check.py \
    --targets d213-audio-utest --sdk /path/to/luban-lite
python bsp/artinchip/tools/d213_hil.py --mode full \
    --confirm-board d213-demo128 --target d213-audio-utest \
    --suite bsp.d213.audio --suite bsp.d213.dmic \
    --sdk /path/to/luban-lite
```

2026-11-08 实机：HIL `20261108-222242-3036`（`d213-audio-utest`）
`bsp.d213.audio` 的 `tc_audio_playback` 与 `bsp.d213.dmic` 的
`tc_dmic_capture` 均 PASS（`tc_dmic: 2048 bytes, min -62 max 18,
periods +1`）。2026-11-09 共享层重构后复测 HIL
`20261109-093546-7576`：两个套件仍 PASS（`tc_dmic: 2048 bytes,
min -73 max 15, periods +1`）。播放用例会短暂播放一段 1 kHz 方波，串联验证
PLL_FRA1→Codec 时钟、TX FIFO DRQ、循环 DMAC 和 PA 控制；同一固件上
手动 `audio_tone 1000 2000` 也无错误返回。但播放用例**不能**证明扬声器
有声学输出，也不能替代人耳/仪器检查。DMIC 的声学输入在该固件的前一
版本上用扬声器实测过（RMS 约 400 → 1150，见 14.6），采集用例本身只
证明时钟/抽取/DRQ/DMA 通路。手动命令见
`14-connectivity-multimedia.md` 14.6。

`bsp.d213.usb` 需要 `BSP_USING_USB1_HOST`（或 `BSP_USING_USB0_HOST`），
使用 `configs/d213-usb-utest.config`（CherryUSB 主机 + EHCI/OHCI + MSC，
当前选 USB1，USB0 留给 BROM/aicupg 刷写）构建：
```sh
python bsp/artinchip/tools/build-check.py \
    --targets d213-usb-utest --sdk /path/to/luban-lite
python bsp/artinchip/tools/d213_hil.py --mode full \
    --confirm-board d213-demo128 --target d213-usb-utest \
    --suite bsp.d213.usb --sdk /path/to/luban-lite
```

2026-11-08 实机：HIL `20261108-074359-19904`（`d213-usb-utest`）
`bsp.d213.usb` PASS（1.68 s）——USB1 根端口枚举、MSC 识别、`sda` 注册、
GETGEOME（512 B 扇区）、两次 8 扇区读一致、扇区 0 `0x55AA` 签名全部
通过。首轮实机暴露了 SET_ADDRESS 阶段 BABBLE，根因是旧版 EHCI core
没有在 SETUP qTD 前清理 D-Cache，修复位置和通用性说明见
`14-connectivity-multimedia.md` 14.2。该用例只读磁盘，不写 U 盘。

`bsp.d213.usbdev` 需要 `BSP_USING_USB0_DEVICE`，使用
`configs/d213-usbdev-utest.config`（在 USB1 Host 配置上增加 USB0
Device + CDC ACM chardev）构建：

```sh
python bsp/artinchip/tools/build-check.py \
    --targets d213-usbdev-utest --sdk /path/to/luban-lite
python bsp/artinchip/tools/d213_hil.py --mode full \
    --confirm-board d213-demo128 --target d213-usbdev-utest \
    --suite bsp.d213.usbdev --sdk /path/to/luban-lite
```

当前状态：构建、ELF/静态检查、打包和实机执行 PASS（HIL
`20261109-053213-7196`，`_utest_testcase=12`）。USB0 接 PC 后
Windows 枚举出 VID `0x33C3`/PID `0x6688` 复合设备和 CDC ACM 串口；
`usb_dev_status` 返回 `configured=1`；`usb_dev_console on` 后可在
CDC 串口上双向执行 MSH 命令，`off` 后 UART 恢复；`d213_usb_console.py`
16 轮 on/off 全 PASS（`20261109-053348`）。注意 USB0 与 BROM/aicupg
共用，实机验证前不要同时使用 aicupg。

`bsp.d213.eth` 需要 `BSP_USING_GMAC0`，使用
`configs/d213-eth-utest.config`（lwIP 2.1.2 + NETDEV + DHCP + GMAC0，
关闭 USB/DFS）构建：

```sh
python bsp/artinchip/tools/build-check.py \
    --targets d213-eth-utest --sdk /path/to/luban-lite
python bsp/artinchip/tools/d213_hil.py --mode full \
    --confirm-board d213-demo128 --target d213-eth-utest \
    --suite bsp.d213.eth --sdk /path/to/luban-lite
```

2026-11-08 实机（网线接路由器）：HIL `20261108-084817-25812`、
`20261108-085007-17888` 均 PASS。`e0` 链路 UP，DHCP 取得
`192.168.31.61/24`，网关 `192.168.31.1`；手动 `ping 192.168.31.1`
4/4 回包，0% 丢包。PHY 为 RTL8201F，自协商 100M 全双工。首轮失败
源于 PE.10/CLK_OUT2 未输出 25 MHz，PHY 不响应 MDIO；修复见
`14-connectivity-multimedia.md` 14.4。2026-11-09 复测（HIL
`20261109-065422-25412`）：链路 UP，DHCP 取得 `192.168.31.61/24`，
网关 `192.168.31.1`，用例 PASS。

`bsp.d213.rtc` 的闹钟单元需要 `RT_USING_ALARM`，
`configs/d213-utest.config` 已开启。2026-11-08 实机：HIL
`20261108-085811-23408`（`d213-utest`），`tc_rtc_advance` 和
`tc_rtc_alarm` 均 PASS；闹钟由 RTC IRQ 50 → `rt_alarm_update()` →
alarm 服务线程触发，回调只到达一次。

`bsp.d213.tsen` 需要 `BSP_USING_TSEN`（会打开 `RT_USING_SENSOR`），
`configs/d213-utest.config` 已开启 CPU 通道。2026-11-08 实机：HIL
`20261108-092325-14520`（`d213-utest` 全量回归），`tc_tsen_read` PASS，
三次读数为 49.8/56.5/57.0 °C；`tsen_status` 显示原始 ADC 1648、
`slope -1132, offset 2435892`，说明 SID eFuse 校准已被读取。
首轮失败（3 s 超时、ADC 值为 0）的原因是只使能了 TSEN 而没有先执行
`hal_adcim_probe()`，TSEN 与 ADCIM 共用模拟前端，修复见
`10-pwm-wdt-rtc.md` 10.4。

`bsp.d213.ce` 需要 `BSP_USING_CE`，使用
`configs/d213-ce-utest.config`（`d213-utest` 配置加 CE 与
`RT_USING_HWCRYPTO`）构建：

```sh
python bsp/artinchip/tools/build-check.py \
    --targets d213-ce-utest --sdk /path/to/luban-lite
python bsp/artinchip/tools/d213_hil.py --mode full \
    --confirm-board d213-demo128 --target d213-ce-utest \
    --suite bsp.d213.ce --sdk /path/to/luban-lite
```

当前状态：构建、ELF/静态检查、打包和实机执行 PASS（HIL
`20261109-034918-80696`，`_utest_testcase=23`）。`bsp.d213.ce`
5/5 单元通过：SHA-256（含分段更新）、SHA-1、MD5、AES-128/256 ECB、
AES-128 CBC、64 B 对齐直通 DMA 和 `CLK_CE` 使能检查。向量为
FIPS-197 AES-128/256、NIST SP 800-38A CBC、RFC 3174/1321/6234 的
SHA/MD5；不依赖外部设备。CE 任务描述符、D-Cache 维护和超时路径
见 `18-decisions.md` ADR-024。

之后用例扩展到 64 扇区并加入 `0x55AA` 签名校验，用于覆盖 IDMA 多描述符
路径。2026-11-07 复测：`utest_run -thread bsp.d213.sdcard` PASS
（`block size 512, sector count 31116288`，64 扇区 = 8 个 4 KiB
描述符），IDMA 读回归记为 HW PASS；`sd_perf 1024` 显示 IDMA 21.8 MB/s、
FIFO 22.8 MB/s，瓶颈在 50 MHz 4-bit 总线。

`bsp.d213.dmac` 需要 `BSP_USING_DMAC`，使用
`configs/d213-dma-utest.config` 构建（`d213-dma-utest` 目标要求 11 个
testcase 符号）。2026-11-08 实机：7/7 unit PASS，HIL
`20261108-020441-21088`；`dma_test 1048576` 170666 KB/s，4 KiB 和
64 KiB 拷贝小于 1 ms tick。

`bsp.d213.uart1.dma` 需要 `BSP_USING_UART_DMA`（依赖 `BSP_USING_DMAC`），
使用 `configs/d213-uart-dma-utest.config` 构建（`d213-uart-dma-utest`
目标同样要求 11 个符号）。2026-11-08 实机：9/9 unit PASS，HIL
`20261108-020309-21624`；4 KiB 传输 11 KB/s（115200 波特率限制）。测试用
MCR 内部回环，不需要外部连线，因此只能证明控制器与 DMAC 通路，不能替代
RS-232 电平/线缆/误码率测试。首轮回归暴露了辅助线程在
`rt_thread_delete()` 上删除已由 idle 回收的动态线程的问题，已修复
（`04219d71cf`）并复测通过。

CAN 外部总线收发仍需要：

- 收发器；
- 两端 120 Ω 终端；
- 第二个 CAN 节点或分析仪；
- 双方相同波特率。

缺少这些条件时，外部总线测试只能记 `SKIP`，不能把控制器 loopback 当作外部总线通过。

2026-11-05 D213 实机验证结果：在没有外部 TX/RX 回环、也没有第二节点时，
`bsp.d213.can0.loopback` 得到 `write=0`、`snd=0 drop=1`、`ackerr=0`，
RX 等待超时。原厂 bare-metal 示例也注明 SELFTEST 需要连接 CAN TX/RX。
因此默认 `d213-utest` 只保留 `bsp.d213.can0.config`；只有准备外部回环或
对端后，才打开 `BSP_UTEST_CAN_LOOPBACK` 并运行 loopback 用例。

### P2 显示和触摸

`bsp.d213.display.vsync` 只验证显示设备、1024x600x32 参数和 VSync 计数递增。以下项目保留为人工/仪器测试：

- 彩条、花屏、闪烁、偏移；
- 双缓冲 pan 和长时间 underflow；
- GE FillRect/BitBlt/Scale/Blend/Rotate；
- GT911 四角坐标校准和多点轨迹。

已有的 `disp_info`、`disp_test`、`ge_test`、`touch_test` MSH 命令保留，用于人工验证。

## 22.4 实机运行

烧录 `artifacts/d213-utest/images/` 下生成的测试镜像，打开 UART0，串口参数 115200 8N1。

```text
msh > utest_list
msh > utest_run -thread components.utilities.utest.uassert
msh > utest_run -thread core.irq
msh > utest_run -thread core.semaphore
msh > utest_run -thread core.mutex
msh > utest_run -thread core.mutex_pi
msh > utest_run -thread bsp.d213.*
```

注意：

- 使用 `-thread` 让测试在独立的 8192 字节 utest 线程中运行，不占用 tshell 的 4096 字节栈；
- 不要用 `utest_run` 不带 `-thread` 跑整套测试；
- BSP 自定义用例的线程/IPC 等待都有上限；主机侧对每个 suite 另有超时；
- `bsp.d213.fpu.context` 应通过，失败即 FAIL，详见 22.6。

官方 `core.timer` 内含 `3 x 60 s` 的 100 定时器压力测试，默认测试配置不启用；需要长时间压力时再单独打开 `RT_UTEST_TIMER` 并放宽主机超时。

## 22.5 主机侧串口脚本

脚本：

```text
bsp/artinchip/tools/d213_utest.py
```

依赖 pyserial：

```sh
python -m pip install pyserial
```

运行：

```sh
python bsp/artinchip/tools/d213_utest.py \
    --port COM7 \
    --log artifacts/d213-utest-serial.log
```

可选参数：

```text
--baud 115200
--timeout 180                 每个 suite 的超时秒数
--suite core.irq              追加/覆盖 suite，可重复
--known-fail <name>           追加已知失败用例
--no-list                     跳过 utest_list
```

脚本会：

- 等待 `msh >`；
- 发送 `utest_list`，检查预期 suite 是否真的进入固件；
- 逐个发送 `utest_run -thread <suite>`；
- 解析 `PASSED` / `FAILED` / `NOT FOUND`；
- 检测启动横幅，判断是否发生复位；
- 检测超时；
- 保存完整串口日志和每个 suite 的耗时。

退出码：

```text
0  PASS
1  FAIL / TIMEOUT / RESET
2  仅存在已知失败（KNOWN-FAIL）
```

脚本不会复位、烧录或擦除目标板。

## 22.6 FPU 上下文回归

官方提交 `9d670b739c72123e90380995f77ec6a2c3f0977f` 已修复 Common Port
的 `fs0..fs11` 和 `fcsr` 上下文保存恢复，并修正栈对齐。本 BSP 已 rebase 到
包含该修复的 master，不附加自己的通用 Port 补丁。

`bsp.d213.fpu.context` 继续保留真实双线程切换检查。主机串口脚本的默认
已知失败列表为空：该用例失败即 FAIL，不再用 `KNOWN-FAIL` 放行，也不再
提示切换到旧的 FPU 集成分支。独立的 FPU 复现和验证分支已完成用途，予以删除。

本章此前的 HIL 记录保留当时的失败结果，属于旧基线的历史证据。
当前官方修复基线仅完成构建和静态检查，没有开发板，尚未重新执行
D213 FPU 实机回归。上板后运行：

```text
utest_run -thread bsp.d213.fpu.context
```

确认报告 PASS，并重跑完整 `bsp.d213.*` 与调度/中断测试。

## 22.7 结果判定

每个结果必须带证据级别：

| 标记 | 含义 |
|---|---|
| BUILD PASS | 只证明编译、链接成功 |
| STATIC PASS | ELF/map/符号/字符串检查通过 |
| PACKAGE PASS | 只证明 `.itb`/`.img` 打包成功 |
| HW PASS | 实机命令完成且输出符合预期 |
| HW FAIL | 实机执行失败 |
| KNOWN-FAIL | 已知且已记录的失败，不能算 HW PASS |
| NOT-TESTED | 未执行或缺少外部条件 |
| SKIP | 条件不足，明确不判通过或失败 |

推荐记录模板：

```text
测试项：
镜像 SHA256：
配置：d213-utest
命令：
预期：
实际：
证据级别：BUILD PASS / STATIC PASS / PACKAGE PASS / HW PASS / HW FAIL / KNOWN-FAIL / NOT-TESTED / SKIP
备注：
```

当前仓库内能提供的证据：

- 各配置的 BUILD PASS、ELF 的 STATIC PASS 和固件 PACKAGE PASS
  （`tools/build-check.py`）；
- P0/P1 套件的 D213 实机结果：见 22.4 各 suite 条目；
- HIL 全流程报告：`bsp/artinchip/artifacts/d213-hil/<run-id>/`，
  包含 `report.json`、串口日志和 utest 日志。

## 22.8 后续扩展

建议按以下顺序补齐：

1. 每次驱动改动后重跑 HIL 全量矩阵（P0 + P1），并记录每个 suite 的日志和耗时；
2. 用第二节点/分析仪完成 CAN 外部总线测试；
3. 增加 GPIO 外部短接环回用例，默认关闭，避免误驱动板级功能引脚；
4. 增加 GE FillRect/BitBlt 的 utest 版本；
5. 增加显示长时间刷新和 underflow 统计；
6. 为 `/nand` 增加掉电注入（格式化/写入过程中断电）和多次复位循环；
7. OTA/A-B 之前先确认 Bootloader 坏块映射，并设计签名、CRC 与回滚；
8. 将主机脚本接入 CI 的硬件 runner，但 BUILD/STATIC 结果不能替代 HW 结果。
