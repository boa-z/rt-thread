/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * D126 non-FPU regression entry points, shared by the msh commands and
 * the utest cases.  None of these functions use the FPU and none of them
 * are executed automatically at boot.
 */

#ifndef __D126_TESTS_H__
#define __D126_TESTS_H__

#include <rtthread.h>

int d126_test_tick(rt_uint32_t delay_ms, rt_uint32_t *delta);
int d126_test_sched(rt_uint32_t loops, rt_uint32_t *counter);
int d126_test_ipc(void);

#endif /* __D126_TESTS_H__ */
