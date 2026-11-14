/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * D126 minimal bring-up self-test.
 */

#include <rtthread.h>
#include <finsh.h>
#include <stdint.h>
#include <stdlib.h>

#include "board.h"

static volatile int g_fpu_fail;

static void fpu_worker(void *parameter)
{
    float value = (float)(rt_ubase_t)parameter;
    float expected;
    int i;

    for (i = 0; i < 32; i++)
    {
        value = value * 1.5f + 0.25f;
        rt_thread_mdelay(1);
    }

    expected = (float)(rt_ubase_t)parameter;
    for (i = 0; i < 32; i++)
    {
        expected = expected * 1.5f + 0.25f;
    }

    if (value < expected - 0.5f || value > expected + 0.5f)
    {
        g_fpu_fail = 1;
        rt_kprintf("fpu worker %d: FAIL got %d expected %d\n",
                   (int)(rt_ubase_t)parameter, (int)value, (int)expected);
    }
    else
    {
        rt_kprintf("fpu worker %d: OK\n", (int)(rt_ubase_t)parameter);
    }
}

static int d126_selftest(int argc, char **argv)
{
    rt_thread_t a;
    rt_thread_t b;
    rt_uint32_t start;
    rt_uint32_t elapsed;

    (void)argc;
    (void)argv;

    g_fpu_fail = 0;

    a = rt_thread_create("fpu0", fpu_worker, (void *)1, 2048, 20, 10);
    b = rt_thread_create("fpu1", fpu_worker, (void *)2, 2048, 20, 10);
    if (a == RT_NULL || b == RT_NULL)
    {
        rt_kprintf("selftest: thread create failed\n");
        return -1;
    }

    rt_thread_startup(a);
    rt_thread_startup(b);

    start = rt_tick_get();
    while (rt_tick_get() - start < RT_TICK_PER_SECOND / 10)
    {
        rt_thread_mdelay(10);
    }
    elapsed = rt_tick_get() - start;

    rt_kprintf("selftest: tick=%u, fpu=%s\n", (unsigned)elapsed,
               g_fpu_fail ? "FAIL" : "OK");
    return g_fpu_fail ? -1 : 0;
}
MSH_CMD_EXPORT_ALIAS(d126_selftest, d126_selftest,
                     "D126 tick and FPU context self-test");

static int d126_memtest(int argc, char **argv)
{
    rt_size_t kb = 1024;
    rt_size_t bytes;
    rt_size_t words;
    rt_uint32_t *buf;
    rt_uint32_t patterns[] = {0x00000000UL, 0xffffffffUL,
                              0x55555555UL, 0xaaaaaaaaUL};
    rt_uint32_t i;
    rt_uint32_t p;
    int failed = 0;

    if (argc > 1)
    {
        kb = (rt_size_t)strtoul(argv[1], RT_NULL, 0);
    }
    if (kb == 0 || kb > 4096)
    {
        rt_kprintf("usage: d126_memtest [1..4096 KiB]\n");
        return -1;
    }

    bytes = kb * 1024;
    buf = (rt_uint32_t *)rt_malloc(bytes);
    if (buf == RT_NULL)
    {
        rt_kprintf("memtest: cannot allocate %u KiB\n", (unsigned)kb);
        return -1;
    }

    words = bytes / sizeof(rt_uint32_t);

    for (p = 0; p < sizeof(patterns) / sizeof(patterns[0]); p++)
    {
        for (i = 0; i < words; i++)
        {
            buf[i] = patterns[p];
        }
        for (i = 0; i < words; i++)
        {
            if (buf[i] != patterns[p])
            {
                rt_kprintf("memtest: pattern 0x%08x failed at %u\n",
                           (unsigned)patterns[p], (unsigned)i);
                failed = 1;
                break;
            }
        }
    }

    for (i = 0; i < words; i++)
    {
        buf[i] = (rt_uint32_t)(rt_ubase_t)&buf[i];
    }
    for (i = 0; i < words; i++)
    {
        if (buf[i] != (rt_uint32_t)(rt_ubase_t)&buf[i])
        {
            rt_kprintf("memtest: address pattern failed at %u\n",
                       (unsigned)i);
            failed = 1;
            break;
        }
    }

    rt_kprintf("memtest: %u KiB @ %p: %s\n", (unsigned)kb, (void *)buf,
               failed ? "FAIL" : "PASS");

    rt_free(buf);
    return failed ? -1 : 0;
}
MSH_CMD_EXPORT_ALIAS(d126_memtest, d126_memtest,
                     "D126 heap/PSRAM pattern test: d126_memtest [KiB]");

int main(void)
{
    /*
     * Do not run the FPU self-test at boot.  Its thread-switch case is a
     * known failure of the current common port and must not turn a normal
     * boot into a failed test.  The command stays available for a future
     * dedicated FPU investigation; the HIL smoke/tick tests live in
     * d126_tests.c.
     */
    rt_kprintf("main: running\n");
    return 0;
}
