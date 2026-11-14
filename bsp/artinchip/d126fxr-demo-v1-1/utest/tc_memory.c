/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Heap/PSRAM tests.
 *
 * The allocator uses the 8 MB region at the end of PSRAM.  The boundary
 * test takes blocks near the top of that region and checks that they
 * stay inside the configured PSRAM window; the stress test churns the
 * allocator and requires the used size to return to its baseline after
 * every round.  The physical 16 MB PSRAM variant still needs a matching
 * bootloader, so this only covers the configured 8 MB heap.
 */

#include <rtthread.h>

#include "board.h"
#include "utest.h"

#define TC_MEM_SIZE         (256 * 1024)
#define TC_MEM_PATTERNS     4

#define PSRAM_LARGE_SIZE    (4 * 1024 * 1024)
#define PSRAM_SECOND_SIZE   (3 * 1024 * 1024)

#define HEAP_STRESS_ROUNDS  8
#define HEAP_STRESS_BLOCKS  16

static rt_uint32_t *tc_mem_buf;

static void tc_memory_patterns(void)
{
    static const rt_uint32_t patterns[TC_MEM_PATTERNS] = {
        0x00000000UL, 0xffffffffUL, 0x55555555UL, 0xaaaaaaaaUL
    };
    rt_size_t words = TC_MEM_SIZE / sizeof(rt_uint32_t);
    rt_size_t i;
    int p;

    tc_mem_buf = (rt_uint32_t *)rt_malloc(TC_MEM_SIZE);
    uassert_not_null(tc_mem_buf);
    if (tc_mem_buf == RT_NULL)
    {
        return;
    }

    for (p = 0; p < TC_MEM_PATTERNS; p++)
    {
        for (i = 0; i < words; i++)
        {
            tc_mem_buf[i] = patterns[p];
        }
        for (i = 0; i < words; i++)
        {
            if (tc_mem_buf[i] != patterns[p])
            {
                uassert_int_equal(tc_mem_buf[i], patterns[p]);
                break;
            }
        }
    }

    for (i = 0; i < words; i++)
    {
        tc_mem_buf[i] = (rt_uint32_t)(rt_ubase_t)&tc_mem_buf[i];
    }
    for (i = 0; i < words; i++)
    {
        if (tc_mem_buf[i] != (rt_uint32_t)(rt_ubase_t)&tc_mem_buf[i])
        {
            uassert_int_equal(tc_mem_buf[i],
                              (rt_uint32_t)(rt_ubase_t)&tc_mem_buf[i]);
            break;
        }
    }

    rt_free(tc_mem_buf);
    tc_mem_buf = RT_NULL;
}

static void tc_psram_boundary(void)
{
    rt_size_t total = 0;
    rt_size_t used0 = 0;
    rt_size_t used1 = 0;
    rt_size_t max_used = 0;
    rt_uint32_t *large;
    rt_uint32_t *second;
    rt_uint32_t *too_big;
    rt_size_t words;
    rt_size_t i;

    rt_memory_info(&total, &used0, &max_used);
    uassert_true(total >= PSRAM_LARGE_SIZE + PSRAM_SECOND_SIZE);
    if (total < PSRAM_LARGE_SIZE + PSRAM_SECOND_SIZE)
    {
        return;
    }

    large = (rt_uint32_t *)rt_malloc(PSRAM_LARGE_SIZE);
    uassert_not_null(large);
    if (large != RT_NULL)
    {
        uassert_true((rt_ubase_t)large >= AIC_PSRAM_BASE);
        uassert_true((rt_ubase_t)large + PSRAM_LARGE_SIZE <=
                     AIC_PSRAM_BASE + AIC_PSRAM_SIZE);

        words = PSRAM_LARGE_SIZE / sizeof(rt_uint32_t);
        for (i = 0; i < words; i++)
        {
            large[i] = 0xa5a5a5a5UL ^ (rt_uint32_t)i;
        }
        for (i = 0; i < words; i++)
        {
            if (large[i] != (0xa5a5a5a5UL ^ (rt_uint32_t)i))
            {
                break;
            }
        }
        uassert_int_equal(i, words);
    }

    second = (rt_uint32_t *)rt_malloc(PSRAM_SECOND_SIZE);
    uassert_not_null(second);
    if (second != RT_NULL)
    {
        uassert_true((rt_ubase_t)second >= AIC_PSRAM_BASE);
        uassert_true((rt_ubase_t)second + PSRAM_SECOND_SIZE <=
                     AIC_PSRAM_BASE + AIC_PSRAM_SIZE);
        second[0] = 0x12345678UL;
        second[PSRAM_SECOND_SIZE / sizeof(rt_uint32_t) - 1] = 0x9abcdef0UL;
        uassert_int_equal(second[0], 0x12345678UL);
        uassert_int_equal(second[PSRAM_SECOND_SIZE / sizeof(rt_uint32_t) - 1],
                          0x9abcdef0UL);
        rt_free(second);
    }

    if (large != RT_NULL)
    {
        rt_free(large);
    }

    /* A request the heap cannot satisfy must fail without side effects. */
    too_big = (rt_uint32_t *)rt_malloc(total);
    uassert_null(too_big);
    rt_memory_info(&total, &used1, &max_used);
    uassert_int_equal(used1, used0);
}

static void tc_heap_stress(void)
{
    rt_uint8_t *blocks[HEAP_STRESS_BLOCKS];
    rt_size_t total = 0;
    rt_size_t used0 = 0;
    rt_size_t used1 = 0;
    rt_size_t max_used = 0;
    int round;

    rt_memory_info(&total, &used0, &max_used);

    for (round = 0; round < HEAP_STRESS_ROUNDS; round++)
    {
        rt_size_t size = (rt_size_t)4096 << (round % 4);
        int count = 4 + (round % 5) * 3;
        int i;

        for (i = 0; i < count; i++)
        {
            blocks[i] = (rt_uint8_t *)rt_malloc(size);
            uassert_not_null(blocks[i]);
            if (blocks[i] == RT_NULL)
            {
                break;
            }

            rt_memset(blocks[i], 0x5a ^ i ^ round, size);
            blocks[i][0] = (rt_uint8_t)i;
            blocks[i][size - 1] = (rt_uint8_t)round;
        }

        while (i-- > 0)
        {
            uassert_int_equal(blocks[i][0], (rt_uint8_t)i);
            uassert_int_equal(blocks[i][size - 1], (rt_uint8_t)round);
            rt_free(blocks[i]);
        }

        rt_memory_info(&total, &used1, &max_used);
        uassert_int_equal(used1, used0);
    }
}

static rt_err_t utest_tc_init(void)
{
    tc_mem_buf = RT_NULL;
    return RT_EOK;
}

static rt_err_t utest_tc_cleanup(void)
{
    if (tc_mem_buf != RT_NULL)
    {
        rt_free(tc_mem_buf);
        tc_mem_buf = RT_NULL;
    }
    return RT_EOK;
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_memory_patterns);
    UTEST_UNIT_RUN(tc_psram_boundary);
    UTEST_UNIT_RUN(tc_heap_stress);
}
UTEST_TC_EXPORT(testcase, "bsp.d126.memory",
                utest_tc_init, utest_tc_cleanup, 30);
