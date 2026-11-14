# 10 PWM、看门狗与 RTC 开发手册

## 10.1 PWM

### 硬件能力

D21x 的 PWM 控制器在本 BSP 配置下提供 4 个通道（`AIC_PWM_CH_NUM = 4`）。demo128 的引脚：

| 通道 | 引脚 | 功能号 | 冲突 |
|---|---|---|---|
| PWM0 | PC.0/PC.1 | 7 | SDMC1 |
| PWM1 | PC.2/PC.3 | 7 | SDMC1 |
| PWM2 | PC.4/PC.5 | 7 | SDMC1 |
| PWM3 | PC.6/PC.7 | 7 | SDMC1 |

PC.7 同时是官方配置中的面板使能脚；启用 PWM3 前必须确认显示/面板需求。

### 驱动组织

- `../libraries/soc/d21x/hal/hal_pwm.c`：原厂 PWM HAL；
- `../libraries/soc/d21x/drivers/drv_pwm.c`：RT-Thread PWM 设备适配。

HAL 提供：

- 递增/递减/增减计数；
- 动作限定器（ZRO/PRD/CAU/CAD/CBU/CBD）；
- 周期和占空比（ns）；
- 极性；
- 中断状态/清除；
- 状态打印。

### Kconfig

```text
BSP_USING_PWM=y
```

会 `select RT_USING_PWM`。

### 使用示例

```c
struct rt_device_pwm *pwm = (struct rt_device_pwm *)rt_device_find("pwm");

/* 1 kHz, 30% duty */
rt_pwm_set(pwm, 0, 1000000, 300000);
rt_pwm_enable(pwm, 0);

/* later */
rt_pwm_disable(pwm, 0);
```

MSH：

```text
msh > pwm_test 0 1000000 500000 3
```

表示 PWM0、周期 1 ms、脉宽 0.5 ms（50%）、持续 3 秒。

### 限制

- 只支持 4 个 PWM 通道；
- 没有接入 PWM 中断到 RT-Thread；
- 没有实现 EPWM（增强 PWM）、CAP（输入捕获）、QEP（正交编码）；
- `hal_pwm_set()` 使用浮点计算分频，依赖 FPU；
- 周期范围受 16 位周期寄存器和时钟分频限制；
- `rt_pwm_set_dead_time()`/`rt_pwm_set_phase()` 未实现，返回 `-RT_EINVAL`；
- 互补输出/死区需要扩展 HAL 适配；
- 没有示波器/频率计实测记录。

### 调试建议

1. 先设置 1 kHz、50%，用示波器确认；
2. 改 10%、90%，确认占空比；
3. 改 100 Hz、100 kHz，确认分频和精度；
4. 检查 PC.0-PC.7 是否被 SDMC1 或面板使能占用；
5. 不要在未确认负载的情况下直接驱动功率器件。

## 10.2 看门狗

### 硬件能力

D21x 看门狗由 32.768 kHz 时钟驱动，支持：

- 计数清零阈值；
- 中断阈值；
- 复位阈值；
- 寄存器写保护；
- 运行状态查询。

本 BSP 只使用复位路径，没有把 WDT 中断接到 RT-Thread 回调。

计数器从 0 递增到 `rst_thd`；只有计数器位于 `clr_thd` 与 `rst_thd`
之间时，写清零命令才会被硬件接受，窗口外的 `KEEPALIVE` 会被静默
忽略。驱动在 `SET_TIMEOUT` 时按原厂规则把 `clr_thd` 限制在
`[0, rst_thd - 2]`，避免窗口缩到 1 秒。

### 驱动组织

- `../libraries/soc/d21x/hal/hal_wdt.c`：原厂 HAL；
- `../libraries/soc/d21x/drivers/drv_wdt.c`：RT-Thread watchdog 适配。

### 使用

```c
rt_device_t wdt = rt_device_find("wdt");

rt_device_open(wdt, RT_DEVICE_FLAG_RDWR);

rt_uint32_t timeout = 10;
rt_device_control(wdt, RT_DEVICE_CTRL_WDT_SET_TIMEOUT, &timeout);
rt_device_control(wdt, RT_DEVICE_CTRL_WDT_START, RT_NULL);

/* 在监控线程中定期喂狗 */
rt_device_control(wdt, RT_DEVICE_CTRL_WDT_KEEPALIVE, RT_NULL);
```

MSH：

```text
msh > wdt_test start 10
msh > wdt_test timeleft
msh > wdt_test feed
msh > wdt_test stop
```

**警告：** `wdt_test start` 会真正启动看门狗。如果没有持续喂狗，板卡将在超时后复位。不要在无人值守的自动化流程中随意执行。

实机状态：`start`/`stop`/`feed`/`GET_TIMELEFT` 已于 2026-11-09 实机
验证（`d213-usbdev-utest`）：窗口内喂狗使剩余时间回到超时值并越过
原截止时间，`stop` 后超过超时时间不复位，`start 3` 不喂狗时 SPL
记录 `Watchdog-Reset` 并正常重启。回归工具
`tools/d213_wdt_check.py`，报告 `20261109-055217`；此前存储 soak 也在
`20261109-035532` 中触发过 start/reset 路径。

### 限制

- 当前只使用通道 0；
- 没有注册 WDT 中断回调；
- 没有实现“中断先报警、再复位”的两阶段策略；
- `RT_DEVICE_CTRL_WDT_GET_TIMELEFT` 依赖 HAL 的秒计数换算，精度为秒级；
- 没有做复位原因记录（RTC/PM 相关寄存器未接入）。

### 安全建议

- 产品中把喂狗放在独立的高优先级线程或定时器；
- 喂狗线程应检查关键任务心跳，而不是无脑喂狗；
- 调试时先确认可以停止看门狗，或准备好复位/重新烧录手段；
- 不要把 `wdt_test start` 放进上电自动执行脚本。

## 10.3 RTC

### 硬件能力

D21x RTC 提供：

- 秒计数；
- 闹钟寄存器；
- 32K 时钟校准；
- 低功耗保持；
- 部分型号支持 IO 输出/唤醒。

本 BSP 实现秒计数读写、单次/周期闹钟和闹钟中断。闹钟通过
RT-Thread `RT_USING_ALARM` 框架使用。

### 驱动组织

- `../libraries/soc/d21x/hal/hal_rtc.c`：原厂 HAL（RTC V1.0 路径），补充
  `hal_rtc_alarm_disable()`；
- `../libraries/soc/d21x/drivers/drv_rtc.c`：RT-Thread RTC 适配、Alarm0 比较器和 IRQ 50。

### Epoch

原厂 Luban-Lite 使用：

```text
AIC_RTC_EPOCH = 1589932800 = 2020-05-20 00:00:00 UTC
```

本 BSP 保持同一 epoch，以便与 Luban-Lite 镜像互换。读写时：

```text
RT-Thread time_t = RTC 秒计数 + 1589932800
```

### 使用

```c
rt_device_t rtc = rt_device_find("rtc");

rt_device_open(rtc, RT_DEVICE_FLAG_RDWR);

time_t now;
rt_device_control(rtc, RT_DEVICE_CTRL_RTC_GET_TIME, &now);

/* set_date()/set_time() 也可使用，前提是 RTC 设备已注册并打开 */
```

MSH：

```text
msh > rtc_test
RTC: 2026-11-04 12:34:56 (epoch 1793...)
```

### 闹钟

使能 `RT_USING_ALARM` 后，使用标准 alarm 框架：

```c
static void alarm_cb(rt_alarm_t alarm, time_t timestamp)
{
    rt_kprintf("alarm at %ld\n", (long)timestamp);
}

struct rt_alarm_setup setup = {0};
setup.flag = RT_ALARM_ONESHOT;
gmtime_r(&target, &setup.wktime);
rt_alarm_t alarm = rt_alarm_create(alarm_cb, &setup);
rt_alarm_start(alarm);
```

驱动行为：

- `set_alarm` 关闭闹钟时清除 `RTC_CTL.ALARM_EN` 和 AON 中断使能；
- 使能时写 `RTC_ALARM0` 比较器；一次性闹钟使用框架给出的完整日期，
  周期闹钟使用“当天或次日”的时间点；
- RTC IRQ（PLIC 50）清除 `AON_IRQ_STA` 并调用 `rt_alarm_update()`；
- `get_alarm` 返回 `RTC_ALARM0` 和中断使能状态。

实机验证：HIL `20261108-085811-23408`（`d213-utest`），
`bsp.d213.rtc` 的 `tc_rtc_advance` 和 `tc_rtc_alarm` 均 PASS。
`tc_rtc_alarm` 设置 3 s 后的一次性闹钟，确认回调只触发一次、回调
时间不早于目标时间，且触发后 `GET_ALARM.enable == 0`。

### 校准

`aic_rtc_init()` 调用：

```c
hal_rtc_cali(AIC_RTC_CLK_RATE);
```

`AIC_RTC_CLK_RATE` 默认 `3276800`（原厂 Kconfig 的 100 倍表示）。如果实际 32K 晶振偏差较大，需要按板卡测量值修改。

### 限制

- 没有掉电唤醒/备份域测试；
- 没有做长期走时精度测试；
- 修改 RTC 时间会影响依赖 `time()` 的组件；
- 不要把 RTC 当作高精度计时器，微秒级计时用 CLINT。

### 测试建议

1. 设置已知时间，断电 10 分钟，上电读回；
2. 与 NTP/GPS 对比 24 小时漂移；
3. 检查 32K 晶振负载电容；
4. 需要掉电唤醒时，再验证 PM 唤醒路径和唤醒源配置。

## 10.4 内部温度传感器（TSEN）

### 硬件能力

D21x 片内 TSEN 提供两个温度通道：

- CPU 温度（channel 0）；
- GPAI 温度（channel 1）。

TSEN 基地址 `0x19253000`，中断号 94。它的模拟前端与 ADCIM
（`0x19250000`）共用模拟公共模块：必须先完成 ADCIM 的上电、时钟使能和
自动校准，TSEN 才能产生有效的 ADC 数据。首次移植时如果只打开 TSEN
而遗漏 ADCIM，现象是读取 3 s 超时、`TSENn_DATA` 始终为 0。

每个芯片的校准参数（CP version、环境温度、ADC 码值）保存在
SID/eFuse（`0x19010000`）。驱动启动时读取一次，换算为斜率和偏置；
空片（CP version = 0）时使用原厂默认值。

### 驱动组织

- `../libraries/soc/d21x/hal/hal_tsen.c`：原厂 TSEN HAL，逐字节复制；
- `../libraries/soc/d21x/hal/hal_adcim.c`：原厂 ADCIM HAL，逐字节复制；
- `../libraries/soc/d21x/hal/hal_efuse.c`：原厂 SID/eFuse HAL，仅使用只读路径；
- `../libraries/soc/d21x/drivers/drv_tsen.c`：RT-Thread sensor 框架适配。

初始化顺序：

```text
hal_adcim_probe()           /* ADCIM 时钟/复位/自动校准 */
hal_tsen_clk_init()         /* TSEN 时钟 + 复位释放 */
aicos_request_irq(94)       /* 数据就绪中断 */
hal_tsen_enable(1)
hal_tsen_curve_fitting()    /* 每通道读取一次 eFuse 校准值 */
rt_hw_sensor_register()     /* temp_tsen_cpu / temp_tsen_gpai */
```

### Kconfig

```text
CONFIG_BSP_USING_TSEN=y
CONFIG_BSP_USING_TSEN_CPU=y
CONFIG_BSP_USING_TSEN_GPAI=n
```

`BSP_USING_TSEN` 会 `select RT_USING_SENSOR`（v1 sensor 框架；不要同时
启用 `RT_USING_SENSOR_V2`）。GPAI 通道默认关闭。

### 设备与使用

sensor 框架会在注册名前面加类别前缀，因此设备名为
`temp_tsen_cpu`（启用 GPAI 后还有 `temp_tsen_gpai`）：

```c
rt_device_t dev = rt_device_find("temp_tsen_cpu");
struct rt_sensor_data data;

rt_device_open(dev, RT_DEVICE_FLAG_RDONLY);
rt_device_read(dev, 0, &data, 1);   /* data.data.temp，单位 0.1 °C */
rt_device_close(dev);
```

驱动实现为单次采样（`AIC_TSEN_MODE_SINGLE`）：每次 `read` 触发一次硬件
采样，数据就绪中断会用信号量唤醒读取线程，超时时间为 3 s。打开设备时
初始化通道，关闭设备时关闭通道。

MSH：

```text
msh > tsen_status
In Thermal Sensor V1.00:
Ch Name          Mode Diff Inv Enable Value  LTA  HTA  OTP
0  tsen_cpu         S    0   0      0  1662    0    0    0
   slope -1132, offset 2439001, pclk 24000000 Hz
```

`Value` 是原始 ADC 码值；`slope`/`offset` 是当前生效的校准参数，
`pclk` 是采样时钟（APB1，24 MHz）。

### 实机验证

HIL `20261108-092325-14520`（`d213-utest` 全量回归），`bsp.d213.tsen`
PASS：连续 3 次读取得到 49.8/56.5/57.0 °C，均在 -40..125 °C 范围内；
`tsen_status` 读到原始 ADC 值 1648，校准参数为 `slope -1132,
offset 2435892`（默认值为 2439001，说明本芯片的 eFuse 校准数据已被
正确读取）。读到的是芯片结温，通常高于环境温度，不能用温枪直接对比，
需要按封装热阻换算。全量回归中除已知的 `bsp.d213.fpu.context`
（上游公共 port 不保存 `fs0..fs11`）外，其余 13 个用例全部 PASS。

### 校准与精度

`hal_tsen_curve_fitting()` 先读 SID 的 CP version：

- `CP version != 0`：用该芯片的 eFuse 单点校准参数；
- `CP version == 0`：保持驱动表中的默认 `slope`/`offset`，精度下降。

D21x 使用 `AIC_SID_DRV_V10` 的 eFuse 字段布局。eFuse 写入需要
`EFUSE_WRITE_SUPPORT`，本 BSP 不启用，也不提供任何写 eFuse 的命令。

### 限制

- 只实现单次采样；周期采样、HTA/LTA/OTP 报警未接入；
- GPAI 通道默认关闭，未实机验证；
- 未做恒温箱/温枪标定，`bsp.d213.tsen` 只证明采样链路、中断和量程；
- ADCIM 自动校准与 TSEN 共用模拟模块；同时启用 GPAI/PSADC/RTP 时
  需要确认模拟通道仲裁；
- 传感器读数受 CPU 负载影响，不能当作环境温度使用。
