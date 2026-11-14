/* SPDX-License-Identifier: Apache-2.0 */
#ifndef D213_HOST_RTTHREAD_H
#define D213_HOST_RTTHREAD_H
#include <stdint.h>
#include <stddef.h>
#define RT_NULL NULL
#define RT_TRUE 1
#define RT_FALSE 0
#define RT_EOK 0
#define RT_ERROR 1
#define RT_EIO 8
#define RT_EINVAL 10
#define RT_EBUSY 7
#define RT_EPERM 11
#define RT_ENOSYS 6
#define RT_ETIMEOUT 2
#define RT_WAITING_FOREVER (-1)
#define RT_IPC_FLAG_PRIO 1
#define RT_IPC_FLAG_FIFO 0
#define RT_IPC_CMD_RESET 0
#define RT_Device_Class_Miscellaneous 0
#define RT_DEVICE_FLAG_RDWR 3
#define RT_DEVICE_FLAG_STANDALONE 8
#define INIT_DEVICE_EXPORT(fn)
typedef int rt_err_t;
typedef int rt_bool_t;
typedef int rt_base_t;
typedef uint8_t rt_uint8_t;
typedef int32_t rt_int32_t;
typedef size_t rt_size_t;
typedef intptr_t rt_ssize_t;
typedef intptr_t rt_off_t;
typedef uintptr_t rt_ubase_t;
typedef uint16_t rt_uint16_t;
typedef uint32_t rt_uint32_t;
typedef void *rt_thread_t;
struct rt_device;
typedef struct rt_device *rt_device_t;
struct rt_device_ops {
    rt_err_t (*init)(rt_device_t);
    rt_err_t (*open)(rt_device_t, rt_uint16_t);
    rt_err_t (*close)(rt_device_t);
    rt_ssize_t (*read)(rt_device_t, rt_off_t, void *, rt_size_t);
    rt_ssize_t (*write)(rt_device_t, rt_off_t, const void *, rt_size_t);
    rt_err_t (*control)(rt_device_t, int, void *);
};
struct rt_device { int ref_count; int type; int flag; const struct rt_device_ops *ops; };
struct rt_mutex { rt_thread_t owner; int depth; };
struct rt_semaphore { int value; };
rt_thread_t rt_thread_self(void);
int rt_interrupt_get_nest(void);
int rt_tick_from_millisecond(int ms);
int rt_mutex_init(struct rt_mutex *, const char *, int);
int rt_mutex_detach(struct rt_mutex *);
int rt_mutex_take(struct rt_mutex *, int);
int rt_mutex_release(struct rt_mutex *);
int rt_sem_init(struct rt_semaphore *, const char *, int, int);
int rt_sem_detach(struct rt_semaphore *);
int rt_sem_take(struct rt_semaphore *, int);
int rt_sem_release(struct rt_semaphore *);
int rt_sem_control(struct rt_semaphore *, int, void *);
rt_base_t rt_hw_interrupt_disable(void);
void rt_hw_interrupt_enable(rt_base_t);
void rt_hw_interrupt_mask(int);
void rt_hw_interrupt_umask(int);
void rt_hw_interrupt_install(int, void (*)(int, void *), void *, const char *);
int rt_device_register(rt_device_t, const char *, int);
#endif
