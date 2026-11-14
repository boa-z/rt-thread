/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * D126 basic regression: CORET tick, scheduling/delay, basic IPC.
 *
 * Every function prints one machine-readable summary line ending in
 * PASS or FAIL.  The HIL parses those lines; the utest cases assert on
 * the return value.  No test touches the FPU.
 */

#include <rtthread.h>
#include <finsh.h>
#include <stdlib.h>

#include "board.h"
#include "d126_tests.h"

#define D126_BOARD_NAME     "d126-demo124-nor"

#define D126_TICK_DEFAULT_MS    100
#define D126_TICK_TOLERANCE     2
#define D126_SCHED_STACK        2048
#define D126_SCHED_DEFAULT      1000
#define D126_SCHED_MAX          100000
#define D126_SCHED_TIMEOUT_MS   10000
#define D126_IPC_TIMEOUT_MS     1000

int d126_test_tick(rt_uint32_t delay_ms, rt_uint32_t *delta)
{
    rt_tick_t start;
    rt_tick_t elapsed;
    rt_tick_t expected;
    rt_tick_t lower;
    rt_tick_t upper;
    int ok;

    if (delay_ms == 0)
    {
        delay_ms = D126_TICK_DEFAULT_MS;
    }

    start = rt_tick_get();
    rt_thread_mdelay(delay_ms);
    elapsed = rt_tick_get() - start;
    expected = rt_tick_from_millisecond(delay_ms);

    /*
     * Lower bound catches a stalled/duplicated tick; the upper bound
     * allows a busy shell and the 1 kHz tick granularity.
     */
    lower = expected > D126_TICK_TOLERANCE
                ? expected - D126_TICK_TOLERANCE : 0;
    upper = expected + expected / 4 + D126_TICK_TOLERANCE;
    ok = (elapsed >= lower) && (elapsed <= upper);

    if (delta != RT_NULL)
    {
        *delta = (rt_uint32_t)elapsed;
    }

    rt_kprintf("d126_tick: delay=%u tick=%u expected=%u %s\n",
               (unsigned)delay_ms, (unsigned)elapsed,
               (unsigned)expected, ok ? "PASS" : "FAIL");
    return ok ? 0 : -1;
}

static struct rt_semaphore d126_sched_done;
static struct rt_semaphore d126_sched_stop;
static struct rt_mutex d126_sched_lock;
static volatile rt_uint32_t d126_sched_counter;
static rt_uint32_t d126_sched_loops;

static void d126_sched_worker(void *parameter)
{
    rt_uint32_t i;

    (void)parameter;

    for (i = 0; i < d126_sched_loops; i++)
    {
        rt_mutex_take(&d126_sched_lock, RT_WAITING_FOREVER);
        d126_sched_counter++;
        rt_mutex_release(&d126_sched_lock);
    }

    rt_sem_release(&d126_sched_done);
    rt_sem_take(&d126_sched_stop, RT_WAITING_FOREVER);
}

int d126_test_sched(rt_uint32_t loops, rt_uint32_t *counter)
{
    rt_thread_t t1 = RT_NULL;
    rt_thread_t t2 = RT_NULL;
    rt_tick_t timeout;
    rt_uint32_t done = 0;
    rt_uint32_t expected;
    rt_uint32_t errors = 0;
    int ok;

    if (loops == 0)
    {
        loops = D126_SCHED_DEFAULT;
    }
    if (loops > D126_SCHED_MAX)
    {
        loops = D126_SCHED_MAX;
    }

    expected = loops * 2;
    d126_sched_loops = loops;
    d126_sched_counter = 0;
    timeout = rt_tick_from_millisecond(D126_SCHED_TIMEOUT_MS);

    if (rt_sem_init(&d126_sched_done, "d126done", 0,
                    RT_IPC_FLAG_FIFO) != RT_EOK)
    {
        errors++;
        goto summary;
    }
    if (rt_sem_init(&d126_sched_stop, "d126stop", 0,
                    RT_IPC_FLAG_FIFO) != RT_EOK)
    {
        errors++;
        rt_sem_detach(&d126_sched_done);
        goto summary;
    }
    if (rt_mutex_init(&d126_sched_lock, "d126lock",
                      RT_IPC_FLAG_PRIO) != RT_EOK)
    {
        errors++;
        rt_sem_detach(&d126_sched_stop);
        rt_sem_detach(&d126_sched_done);
        goto summary;
    }

    t1 = rt_thread_create("d126w0", d126_sched_worker, RT_NULL,
                          D126_SCHED_STACK, 11, 10);
    t2 = rt_thread_create("d126w1", d126_sched_worker, RT_NULL,
                          D126_SCHED_STACK, 12, 10);
    if (t1 == RT_NULL || t2 == RT_NULL)
    {
        errors++;
        if (t1 != RT_NULL)
        {
            rt_thread_delete(t1);
        }
        if (t2 != RT_NULL)
        {
            rt_thread_delete(t2);
        }
    }
    else
    {
        rt_thread_startup(t1);
        rt_thread_startup(t2);

        while (done < 2 &&
               rt_sem_take(&d126_sched_done, timeout) == RT_EOK)
        {
            done++;
        }

        if (done != 2 || d126_sched_counter != expected)
        {
            errors++;
        }

        /*
         * Both workers release d126_sched_done and then block on
         * d126_sched_stop, so deleting them here is safe.  Do not release
         * the stop semaphore: a worker that returns from its entry is
         * auto-freed by the idle thread and a later rt_thread_delete()
         * would trip the object-class assertion.
         */
        rt_thread_mdelay(20);
        rt_thread_delete(t1);
        rt_thread_delete(t2);
    }

    rt_mutex_detach(&d126_sched_lock);
    rt_sem_detach(&d126_sched_stop);
    rt_sem_detach(&d126_sched_done);

summary:
    ok = (errors == 0);
    if (counter != RT_NULL)
    {
        *counter = d126_sched_counter;
    }
    rt_kprintf("d126_sched: loops=%u counter=%u expected=%u %s\n",
               (unsigned)loops, (unsigned)d126_sched_counter,
               (unsigned)expected, ok ? "PASS" : "FAIL");
    return ok ? 0 : -1;
}

int d126_test_ipc(void)
{
    struct rt_semaphore sem;
    struct rt_mailbox mb;
    rt_ubase_t mb_pool[4];
    rt_ubase_t marker = 0x5a5a1234UL;
    rt_ubase_t received = 0;
    int sem_ok = 0;
    int mb_ok = 0;
    int ok;

    if (rt_sem_init(&sem, "d126sem", 0, RT_IPC_FLAG_FIFO) != RT_EOK)
    {
        rt_kprintf("d126_ipc: sem=INIT_FAIL mb=SKIP FAIL\n");
        return -1;
    }
    if (rt_sem_release(&sem) == RT_EOK &&
        rt_sem_take(&sem, rt_tick_from_millisecond(D126_IPC_TIMEOUT_MS))
            == RT_EOK)
    {
        sem_ok = 1;
    }
    rt_sem_detach(&sem);

    if (rt_mb_init(&mb, "d126mb", &mb_pool[0],
                   sizeof(mb_pool), RT_IPC_FLAG_FIFO) != RT_EOK)
    {
        rt_kprintf("d126_ipc: sem=%s mb=INIT_FAIL FAIL\n",
                   sem_ok ? "PASS" : "FAIL");
        return -1;
    }
    if (rt_mb_send(&mb, marker) == RT_EOK &&
        rt_mb_recv(&mb, &received,
                   rt_tick_from_millisecond(D126_IPC_TIMEOUT_MS)) == RT_EOK &&
        received == marker)
    {
        mb_ok = 1;
    }
    rt_mb_detach(&mb);

    ok = sem_ok && mb_ok;
    rt_kprintf("d126_ipc: sem=%s mb=%s %s\n",
               sem_ok ? "PASS" : "FAIL", mb_ok ? "PASS" : "FAIL",
               ok ? "PASS" : "FAIL");
    return ok ? 0 : -1;
}

#ifdef RT_USING_FINSH

static int cmd_d126_info(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    rt_kprintf("d126_info: board=%s cpu=e907f psram=%uMB tick_hz=%u "
               "uptime_s=%lu\n", D126_BOARD_NAME,
               (unsigned)(AIC_PSRAM_SIZE / (1024 * 1024)),
               (unsigned)RT_TICK_PER_SECOND,
               (unsigned long)(rt_tick_get() / RT_TICK_PER_SECOND));
    return 0;
}
MSH_CMD_EXPORT_ALIAS(cmd_d126_info, d126_info,
                     "D126 board identity and uptime");

static int cmd_d126_tick(int argc, char **argv)
{
    rt_uint32_t delay_ms = D126_TICK_DEFAULT_MS;

    if (argc > 1)
    {
        delay_ms = (rt_uint32_t)strtoul(argv[1], RT_NULL, 0);
    }

    return d126_test_tick(delay_ms, RT_NULL);
}
MSH_CMD_EXPORT_ALIAS(cmd_d126_tick, d126_tick,
                     "D126 CORET tick test: d126_tick [delay_ms]");

static int cmd_d126_sched(int argc, char **argv)
{
    rt_uint32_t loops = D126_SCHED_DEFAULT;

    if (argc > 1)
    {
        loops = (rt_uint32_t)strtoul(argv[1], RT_NULL, 0);
    }

    return d126_test_sched(loops, RT_NULL);
}
MSH_CMD_EXPORT_ALIAS(cmd_d126_sched, d126_sched,
                     "D126 scheduling/mutex test: d126_sched [loops]");

static int cmd_d126_ipc(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return d126_test_ipc();
}
MSH_CMD_EXPORT_ALIAS(cmd_d126_ipc, d126_ipc,
                     "D126 semaphore/mailbox IPC test");

#endif /* RT_USING_FINSH */
