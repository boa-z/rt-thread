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

- `vendor/hal/hal_pwm.c`：原厂 PWM HAL；
- `drivers/drv_pwm.c`：RT-Thread PWM 设备适配。

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

### 驱动组织

- `vendor/hal/hal_wdt.c`：原厂 HAL；
- `drivers/drv_wdt.c`：RT-Thread watchdog 适配。

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
msh > wdt_test feed
msh > wdt_test stop
```

**警告：** `wdt_test start` 会真正启动看门狗。如果没有持续喂狗，板卡将在超时后复位。不要在无人值守的自动化流程中随意执行。

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

本 BSP 只实现秒计数读写。

### 驱动组织

- `vendor/hal/hal_rtc.c`：原厂 HAL（RTC V1.0 路径）；
- `drivers/drv_rtc.c`：RT-Thread RTC 适配。

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

### 校准

`aic_rtc_init()` 调用：

```c
hal_rtc_cali(AIC_RTC_CLK_RATE);
```

`AIC_RTC_CLK_RATE` 默认 `3276800`（原厂 Kconfig 的 100 倍表示）。如果实际 32K 晶振偏差较大，需要按板卡测量值修改。

### 限制

- 闹钟读写未实现；
- RTC 中断未注册；
- 没有掉电唤醒/备份域测试；
- 没有做长期走时精度测试；
- 修改 RTC 时间会影响依赖 `time()` 的组件；
- 不要把 RTC 当作高精度计时器，微秒级计时用 CLINT。

### 测试建议

1. 设置已知时间，断电 10 分钟，上电读回；
2. 与 NTP/GPS 对比 24 小时漂移；
3. 检查 32K 晶振负载电容；
4. 若使用闹钟，先实现并验证中断路径。
