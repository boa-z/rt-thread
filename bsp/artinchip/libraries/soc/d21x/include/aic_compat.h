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

#include "aic_cache.h"

/*
 * The vendor sources (including the CherryUSB AIC glue in
 * components/drivers/usb/cherryusb/port/ehci) select their RT-Thread
 * integration with KERNEL_RTTHREAD, which the Luban-Lite Kconfig defines
 * globally.  The upstream RT-Thread tree has no such symbol, so this
 * compatibility header provides it for the curated vendor sources.
 */
#ifndef KERNEL_RTTHREAD
#define KERNEL_RTTHREAD
#endif

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

typedef uintptr_t ptr_t;

#ifndef BIT
#define BIT(n)              (1U << (n))
#endif

#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n, d)  (((n) + (d) - 1) / (d))
#endif
#ifndef DIV_ROUND_CLOSEST
#define DIV_ROUND_CLOSEST(n, d) (((n) + (d) / 2) / (d))
#endif
#ifndef roundup
#define roundup(x, y)       ((((x) + ((y) - 1)) / (y)) * (y))
#endif
#ifndef rounddown
#define rounddown(x, y)     (((x) / (y)) * (y))
#endif
#ifndef __ALIGN_MASK
#define __ALIGN_MASK(x, mask) (((x) + (mask)) & ~(mask))
#endif

/*
 * min()/max() are macros in some RT-Thread headers and static inlines in
 * others.  The curated vendor HAL uses the Linux-style macro form.
 */
#ifndef min
#define min(a, b)           ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b)           ((a) > (b) ? (a) : (b))
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

/* Vendor BCD version formatting, used by the TSEN status print. */
#define EXPAND_BCD_VER(v)    ({ \
    static char ver[8]; \
    int i = 0; \
    memset(ver, 0, 8); \
    if ((v) & 0xF000) \
        ver[i++] = (((v) >> 12) & 0xF) + 0x30; \
    ver[i++] = (((v) >> 8) & 0xF) + 0x30; \
    ver[i++] = '.'; \
    ver[i++] = (((v) >> 4) & 0xF) + 0x30; \
    ver[i]   = ((v) & 0xF) + 0x30; \
    ver; \
})

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

/*
 * Millisecond-resolution microsecond timestamp.  The curated HAL only
 * uses this for polling timeouts, so tick granularity is sufficient.
 */
static inline u64 aic_get_time_us(void)
{
    return (u64)rt_tick_get_millisecond() * 1000ULL;
}

static inline void aic_udelay(u32 us)
{
    rt_hw_us_delay(us);
}

#define udelay(us)          aic_udelay(us)
#define mdelay(ms)          rt_thread_mdelay(ms)
#define msecs_to_jiffies(ms) ((ms) * RT_TICK_PER_SECOND / 1000)

/* ------------------------------------------------------------------ */
/* Primitives used by the curated DMA-controller HAL                   */
/* ------------------------------------------------------------------ */

/*
 * This BSP has no MMU: the CPU address is the address the DMA engine
 * sees, so __pa() only narrows the type to the controller's 32-bit
 * address fields.
 */
#define __ALIGNED(x)        __attribute__((aligned(x)))
#define __pa(x)             ((u32)(unsigned long)(x))

#define aicos_local_irq_save(pstate) \
    do { *(pstate) = (unsigned long)rt_hw_interrupt_disable(); } while (0)
#define aicos_local_irq_restore(state) \
    rt_hw_interrupt_enable((rt_base_t)(state))

static inline void aicos_dcache_clean_range(void *addr, unsigned long size)
{
    aic_dcache_clean_range((uintptr_t)addr, (size_t)size);
}

static inline void aicos_dcache_invalid_range(void *addr, unsigned long size)
{
    aic_dcache_invalidate_range((uintptr_t)addr, (size_t)size);
}

static inline void aicos_dcache_clean_invalid_range(void *addr,
                                                    unsigned long size)
{
    aic_dcache_clean_invalidate_range((uintptr_t)addr, (size_t)size);
}

/*
 * The CherryUSB AIC device port uses the C906 SDK spelling.
 */
static inline void csi_dcache_clean_range(phy_addr_t addr, unsigned long size)
{
    aic_dcache_clean_range((uintptr_t)addr, (size_t)size);
}

static inline void aicos_udelay(u32 us)
{
    aic_udelay(us);
}

static inline void aicos_mdelay(u32 ms)
{
    rt_thread_mdelay(ms);
}

static inline void aicos_msleep(u32 ms)
{
    rt_thread_mdelay(ms);
}

typedef void *aicos_thread_t;
typedef void (*aic_thread_entry_t)(void *args);

static inline aicos_thread_t aicos_thread_create(const char *name,
                                                 uint32_t stack_size,
                                                 uint32_t prio,
                                                 aic_thread_entry_t entry,
                                                 void *args)
{
    rt_thread_t thread;

    thread = rt_thread_create(name, entry, args, stack_size, prio, 10);
    if (thread != RT_NULL)
        rt_thread_startup(thread);

    return (aicos_thread_t)thread;
}

/* Order MMIO writes before the DMA engine is told to start. */
static inline void aicos_dma_sync(void)
{
    __asm__ volatile("fence rw, rw" ::: "memory");
}

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

static inline void *aicos_malloc_align(unsigned int type, size_t size,
                                       size_t align)
{
    (void)type;
    return rt_malloc_align(size, align);
}

static inline void aicos_free_align(unsigned int type, void *ptr)
{
    (void)type;
    rt_free_align(ptr);
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

/*
 * Counting semaphore API, same timeout contract as the vendor
 * aic_osal_rtthread.h: the timeout is in milliseconds.
 */
static inline aicos_sem_t aicos_sem_create(uint32_t initial_count)
{
    return (aicos_sem_t)rt_sem_create("aic_sem", initial_count,
                                      RT_IPC_FLAG_FIFO);
}

static inline void aicos_sem_delete(aicos_sem_t sem)
{
    rt_sem_delete((rt_sem_t)sem);
}

static inline int aicos_sem_take(aicos_sem_t sem, uint32_t msec)
{
    rt_err_t result;

    if (msec == AICOS_WAIT_FOREVER)
        result = rt_sem_take((rt_sem_t)sem, RT_WAITING_FOREVER);
    else
        result = rt_sem_take((rt_sem_t)sem, rt_tick_from_millisecond(msec));

    if (result == -RT_ETIMEOUT)
        return -ETIMEDOUT;
    if (result == -RT_ERROR)
        return -EINVAL;

    return 0;
}

static inline int aicos_sem_give(aicos_sem_t sem)
{
    return (int)rt_sem_release((rt_sem_t)sem);
}

static inline int aicos_sem_reset(aicos_sem_t sem, unsigned long val)
{
    return (int)rt_sem_control((rt_sem_t)sem, RT_IPC_CMD_RESET, (void *)val);
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
typedef irqreturn_t (*irq_handler_t)(int irq, void *arg);

/*
 * The vendor HAL passes raw PLIC source numbers (USB_HOSTx_IRQn and
 * friends), while the RT-Thread RISC-V common interrupt controller
 * expects the global vector: PLIC sources start after the local INTC
 * vectors.  Mapping happens here so the imported vendor sources need no
 * changes.
 */
#ifdef __riscv
extern int irq_plic_base;
#ifndef AIC_IRQ_PLIC
#define AIC_IRQ_PLIC(hwirq) (irq_plic_base + (hwirq))
#endif
#else
#define AIC_IRQ_PLIC(hwirq) (hwirq)
#endif

static inline int aicos_request_irq(int irq, aicos_irq_handler_t handler,
                                    unsigned long flags, const char *name,
                                    void *arg)
{
    int vector = AIC_IRQ_PLIC(irq);

    (void)flags;
    rt_hw_interrupt_install(vector, (rt_isr_handler_t)handler, arg, name);
    rt_hw_interrupt_umask(vector);
    return 0;
}

static inline void aicos_irq_enable(int irq)
{
    rt_hw_interrupt_umask(AIC_IRQ_PLIC(irq));
}

static inline void aicos_irq_disable(int irq)
{
    rt_hw_interrupt_mask(AIC_IRQ_PLIC(irq));
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
#define SID_BASE            0x19010000UL
#define ADCIM_BASE          0x19250000UL
#define GPAI_BASE           0x19251000UL
#define TSEN_BASE           0x19253000UL
#define I2C0_BASE           0x19220000UL
#define I2C1_BASE           0x19221000UL
#define I2C2_BASE           0x19222000UL
#define I2C3_BASE           0x19223000UL
#define CAN0_BASE           0x19230000UL
#define CAN1_BASE           0x19231000UL
#define PWM_BASE            0x19240000UL
#define AUDIO_BASE          0x18610000UL
#define SYSCFG_BASE         0x18000000UL
#define CE_BASE             0x10020000UL
#define USB_DEV_BASE        0x10200000UL
#define USB_HOST0_BASE      0x10210000UL
#define USB_HOST1_BASE      0x10220000UL
#define GMAC0_BASE          0x10280000UL
#define GMAC1_BASE          0x10290000UL

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
#define GPAI_IRQn           92
#define WDT_IRQn            64
#define RTC_IRQn            50
#define TSEN_IRQn           94
#define GPIO_IRQn           68
#define DE_IRQn             59
#define AUDIO_IRQn          54
#define USB_DEV_IRQn        34
#define USB_HOST0_EHCI_IRQn 35
#define USB_HOST0_OHCI_IRQn 36
#define USB_HOST1_EHCI_IRQn 37
#define USB_HOST1_OHCI_IRQn 38
#define GMAC0_IRQn          39
#define GMAC1_IRQn          40
#define SDMC0_IRQn          46
#define SDMC1_IRQn          47
#define SDMC2_IRQn          48
#define DMA_IRQn            32
/* Raw PLIC source number; aicos_request_irq() maps it. */
#define GE_IRQn             60

/* ------------------------------------------------------------------ */
/* D21x build-time peripheral configuration                            */
/* ------------------------------------------------------------------ */

#define AIC_RTC_DRV_V10
#define AIC_WDT_DRV_V10
#define AIC_DE_DRV_V10
#define AIC_AUDIO_DRV_V10
#define AIC_GMAC_DRV_V10
#define AIC_TSEN_DRV_V10
#define AIC_SID_DRV_V10
#define AIC_ADCIM_DRV_V10
#define AIC_GPAI_DRV_V10
#define AIC_PWM_CH_NUM      4
#define AIC_GPAI_CH_NUM     8
#define AIC_GPAI_CH_NUM_MASK 0x7
#define AIC_RTC_CLK_RATE    3276800
#define AIC_TSEN_CH_NUM     2

/* SID/eFuse controller parameters from the D21x vendor Kconfig. */
#define EFUSE_MAX_WORD      64
#define EFUSE_TIMING_VALUE  0x0402ffd8

/* Enabled IP selection is shared; PHY wiring belongs to the board. */
#ifdef BSP_USING_GMAC0
#define AIC_USING_GMAC0
#define AIC_GMAC_DRV
#ifndef AIC_GMAC_DEV_NUM
#define AIC_GMAC_DEV_NUM 1
#endif
#include "aic_board_config.h"
#endif

/*
 * USB0 role selection.  The CherryUSB AIC device port uses the
 * Luban-Lite symbol to decide whether to flip the PHY role bit.
 */
#ifdef BSP_USING_USB0_DEVICE
#define AIC_USING_USB0_DEVICE
#endif

/*
 * The TSEN HAL derives per-chip slope/offset from the SID eFuse.  The
 * SID read path is code shared with the sensor (and only compiled when
 * TSEN is enabled), so define both drivers together.
 */
#ifdef BSP_USING_TSEN
#define AIC_USING_TSEN
#define AIC_TSEN_DRV
#define AIC_SID_DRV
#ifdef BSP_USING_TSEN_CPU
#define AIC_USING_TSEN_CPU
#endif
#ifdef BSP_USING_TSEN_GPAI
#define AIC_USING_TSEN_GPAI
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif /* __AIC_COMPAT_H__ */
