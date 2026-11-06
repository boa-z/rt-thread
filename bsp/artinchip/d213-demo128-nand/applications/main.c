/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D213 minimal BSP entry.
 */

#include <rtthread.h>

int main(void)
{
    rt_kprintf("\n");
    rt_kprintf("ArtInChip D213 demo128 RT-Thread BSP\n");
    rt_kprintf("CPU: T-Head C906 (RV64GC), tick: %d Hz\n", RT_TICK_PER_SECOND);

#ifdef BSP_USING_UTEST
    rt_kprintf("utest: run 'utest_run -thread bsp.d213.*'\n");
#endif

    /*
     * Keep the boot path free of known-failing self tests.  The FPU
     * context regression is available as the aic_selftest command and
     * as the bsp.d213.fpu.context utest case.
     */
    rt_kprintf("main: running\n");
    return 0;
}
