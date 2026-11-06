/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal Luban-Lite compatibility layer used only by the curated
 * ArtInChip HAL sources in this directory.
 *
 * This header deliberately does NOT emulate the vendor OS abstraction
 * layer.  It provides the small set of Linux-style primitives that the
 * selected HAL files need, mapped onto RT-Thread.
 */

#ifndef __AIC_COMPAT_H__
#define __AIC_COMPAT_H__

#include <rtthread.h>
#include <rthw.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Basic types (D21x is a 64-bit SoC)                                  */
/* ------------------------------------------------------------------ */

typedef int8_t   s8;
typedef uint8_t  u8;
typedef int16_t  s16;
typedef uint16_t u16;
typedef int32_t  s32;
typedef uint32_t u32;
typedef int64_t  s64;
typedef uint64_t u64;
typedef unsigned long ulong;
typedef uint32_t dma_addr_t;
typedef uint32_t phy_addr_t;

#define BITS_PER_LONG       64
#define ARCH_DMA_MINALIGN   64
#define CACHE_LINE_SIZE     64

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t)&((TYPE *)0)->MEMBER)
#endif

#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

#define ARRAY_SIZE(a)       (sizeof(a) / sizeof((a)[0]))

#ifndef BIT
#define BIT(n)              (1U << (n))
#endif
#define BIT_UL(n)           (1UL << (n))
#define GENMASK(h, l) \
    (((~0UL) << (l)) & (~0UL >> (BITS_PER_LONG - 1 - (h))))
#define GENMASK_UL(h, l)    GENMASK(h, l)

#ifndef likely
#define likely(x)           __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define unlikely(x)         __builtin_expect(!!(x), 0)
#endif

/* ------------------------------------------------------------------ */
/* MMIO helpers                                                        */
/* ------------------------------------------------------------------ */

#define readb(addr)         (*(volatile u8 *)(uintptr_t)(addr))
#define readw(addr)         (*(volatile u16 *)(uintptr_t)(addr))
#define readl(addr)         (*(volatile u32 *)(uintptr_t)(addr))
#define writeb(val, addr)   (*(volatile u8 *)(uintptr_t)(addr) = (u8)(val))
#define writew(val, addr)   (*(volatile u16 *)(uintptr_t)(addr) = (u16)(val))
#define writel(val, addr)   (*(volatile u32 *)(uintptr_t)(addr) = (u32)(val))

#define writel_bits(val, mask, shift, addr)         \
    do {                                             \
        u32 __tmp = readl(addr);                     \
        __tmp &= ~(mask);                            \
        __tmp |= ((val) << (shift)) & (mask);        \
        writel(__tmp, addr);                         \
    } while (0)

/* ------------------------------------------------------------------ */
/* Error codes                                                         */
/* ------------------------------------------------------------------ */

#ifndef EOK
#define EOK                 0
#endif
#ifndef EINVAL
#define EINVAL              22
#endif
#ifndef ERANGE
#define ERANGE              34
#endif
#ifndef EOPNOTSUPP
#define EOPNOTSUPP          95
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT           110
#endif
#ifndef ENOMEM
#define ENOMEM              12
#endif

#ifndef _IOWR
#define _IOWR(x, y, z)      (((x) << 8) | (y))
#endif
#ifndef _IOW
#define _IOW(x, y, z)       (((x) << 8) | (y))
#endif
#ifndef _IOR
#define _IOR(x, y, z)       (((x) << 8) | (y))
#endif
#ifndef _IO
#define _IO(x, y)           (((x) << 8) | (y))
#endif

/* ------------------------------------------------------------------ */
/* Logging                                                             */
/* ------------------------------------------------------------------ */

#define pr_err(fmt, ...)    rt_kprintf("[aic] " fmt, ##__VA_ARGS__)
#define pr_warn(fmt, ...)   rt_kprintf("[aic] " fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...)   rt_kprintf("[aic] " fmt, ##__VA_ARGS__)
#define pr_debug(fmt, ...)  do { } while (0)

#define hal_log_err         pr_err
#define hal_log_warn        pr_warn
#define hal_log_info        pr_info
#define hal_log_debug       pr_debug

/* ------------------------------------------------------------------ */
/* Time helpers                                                        */
/* ------------------------------------------------------------------ */

static inline u32 aic_get_time_ms(void)
{
    return (u32)rt_tick_get_millisecond();
}

static inline void aic_udelay(u32 us)
{
    rt_hw_us_delay(us);
}

#define udelay(us)          aic_udelay(us)
#define mdelay(ms)          rt_thread_mdelay(ms)
#define msecs_to_jiffies(ms) ((ms) * RT_TICK_PER_SECOND / 1000)

/* 64-bit divide helper, same contract as the Linux do_div(). */
#define do_div(n, base) ({          \
    u32 __base = (base);            \
    u32 __rem;                      \
    __rem = ((u64)(n)) % __base;    \
    (n) = ((u64)(n)) / __base;      \
    __rem;                          \
})

/* ------------------------------------------------------------------ */
/* ArtInChip OS abstraction shim (GE/display HAL)                      */
/* ------------------------------------------------------------------ */

typedef enum {
    IRQ_NONE = 0,
    IRQ_HANDLED = 1,
} irqreturn_t;

#define AICOS_WAIT_FOREVER      RT_WAITING_FOREVER
#define MEM_CMA                 0U
#define MEM_DEFAULT             1U

typedef void *aicos_thread_t;
typedef struct rt_semaphore *aicos_sem_t;
typedef struct rt_mutex *aicos_mutex_t;
typedef struct rt_event *aicos_event_t;
typedef void *aicos_queue_t;
typedef void *aicos_wqueue_t;

struct device;

static inline void *aicos_malloc(unsigned int type, size_t size)
{
    (void)type;
    return rt_malloc(size);
}

static inline void aicos_free(unsigned int type, void *ptr)
{
    (void)type;
    rt_free(ptr);
}

static inline aicos_mutex_t aicos_mutex_create(void)
{
    return (aicos_mutex_t)rt_mutex_create("aic", RT_IPC_FLAG_PRIO);
}

static inline void aicos_mutex_delete(aicos_mutex_t mutex)
{
    rt_mutex_delete((rt_mutex_t)mutex);
}

static inline int aicos_mutex_take(aicos_mutex_t mutex, rt_int32_t timeout)
{
    return rt_mutex_take((rt_mutex_t)mutex, timeout);
}

static inline int aicos_mutex_give(aicos_mutex_t mutex)
{
    return rt_mutex_release((rt_mutex_t)mutex);
}

static inline aicos_event_t aicos_event_create(void)
{
    return (aicos_event_t)rt_event_create("aic", RT_IPC_FLAG_FIFO);
}

static inline void aicos_event_delete(aicos_event_t event)
{
    rt_event_delete((rt_event_t)event);
}

static inline int aicos_event_send(aicos_event_t event, rt_uint32_t set)
{
    return rt_event_send((rt_event_t)event, set);
}

static inline int aicos_event_recv(aicos_event_t event, rt_uint32_t set,
                                   rt_uint32_t *recved, rt_int32_t timeout)
{
    return rt_event_recv((rt_event_t)event, set,
                         RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                         timeout, recved);
}

static inline size_t aicos_enter_critical_section(void)
{
    return (size_t)rt_hw_interrupt_disable();
}

static inline void aicos_leave_critical_section(size_t flag)
{
    rt_hw_interrupt_enable((rt_base_t)flag);
}

typedef irqreturn_t (*aicos_irq_handler_t)(int irq, void *arg);

static inline int aicos_request_irq(int irq, aicos_irq_handler_t handler,
                                    unsigned long flags, const char *name,
                                    void *arg)
{
    (void)flags;
    rt_hw_interrupt_install(irq, (rt_isr_handler_t)handler, arg, name);
    rt_hw_interrupt_umask(irq);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Interrupt return type is declared above for the OSAL shim.         */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Parameter checks (vendor convention)                                */
/* ------------------------------------------------------------------ */

#ifndef BUG
#define BUG() \
    do { rt_kprintf("BUG: %s:%d\n", __func__, __LINE__); while (1) { } } while (0)
#endif

#ifndef CHECK_PARAM
#define CHECK_PARAM(x, ret) \
    do { if (!(x)) return (ret); } while (0)
#endif

#ifndef CHECK_PARAM_RET
#define CHECK_PARAM_RET(x) \
    do { if (!(x)) return; } while (0)
#endif

#ifndef CHECK_RET_WITH_RET
#define CHECK_RET_WITH_RET(x, ret) \
    do { if (!(x)) { pr_err("%s:%d check failed\n", __func__, __LINE__); return (ret); } } while (0)
#endif

#ifndef CHECK_RET_WITH_GOTO
#define CHECK_RET_WITH_GOTO(x, label) \
    do { if (!(x)) { pr_err("%s:%d check failed\n", __func__, __LINE__); goto label; } } while (0)
#endif

/* ------------------------------------------------------------------ */
/* D21x memory map                                                     */
/* ------------------------------------------------------------------ */

#define CMU_BASE            0x18020000UL
#define GPIO_BASE           0x18700000UL
#define LVDS_BASE           0x18810000UL
#define DE_BASE             0x18A00000UL
#define GE_BASE             0x18B00000UL
#define UART0_BASE          0x18710000UL
#define UART1_BASE          0x18711000UL
#define UART2_BASE          0x18712000UL
#define UART3_BASE          0x18713000UL
#define UART4_BASE          0x18714000UL
#define UART5_BASE          0x18715000UL
#define UART6_BASE          0x18716000UL
#define UART7_BASE          0x18717000UL
#define WDT_BASE            0x19000000UL
#define RTC_BASE            0x19030000UL
#define I2C0_BASE           0x19220000UL
#define I2C1_BASE           0x19221000UL
#define I2C2_BASE           0x19222000UL
#define I2C3_BASE           0x19223000UL
#define CAN0_BASE           0x19230000UL
#define CAN1_BASE           0x19231000UL
#define PWM_BASE            0x19240000UL

#define UART0_IRQn          76
#define UART1_IRQn          77
#define UART2_IRQn          78
#define UART3_IRQn          79
#define UART4_IRQn          80
#define UART5_IRQn          81
#define UART6_IRQn          82
#define UART7_IRQn          83
#define I2C0_IRQn           84
#define I2C1_IRQn           85
#define I2C2_IRQn           86
#define I2C3_IRQn           87
#define CAN0_IRQn           88
#define CAN1_IRQn           89
#define PWM_IRQn            90
#define WDT_IRQn            64
#define RTC_IRQn            50
#define GPIO_IRQn           68
#define DE_IRQn             59
#ifdef __riscv
extern int irq_plic_base;
#define AIC_IRQ_PLIC(hwirq) (irq_plic_base + (hwirq))
#define GE_IRQn             AIC_IRQ_PLIC(60)
#else
#define GE_IRQn             60
#endif

/* ------------------------------------------------------------------ */
/* D21x build-time peripheral configuration                            */
/* ------------------------------------------------------------------ */

#define AIC_RTC_DRV_V10
#define AIC_WDT_DRV_V10
#define AIC_DE_DRV_V10
#define AIC_PWM_CH_NUM      4
#define AIC_RTC_CLK_RATE    3276800

#ifdef __cplusplus
}
#endif

#endif /* __AIC_COMPAT_H__ */
