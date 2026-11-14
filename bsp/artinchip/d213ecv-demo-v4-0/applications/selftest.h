/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __AIC_SELFTEST_H__
#define __AIC_SELFTEST_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Run the two-thread FPU context regression shared by the aic_selftest
 * command and the BSP utest case.
 *
 * Returns RT_EOK when both workers preserved fs0..fs11.  On failure,
 * *failed_reg is set to the 1-based index of the first mismatching
 * callee-saved FPU register.  The function always returns within a
 * bounded time.
 */
rt_err_t aic_fpu_context_check(rt_uint32_t *failed_reg);

#ifdef __cplusplus
}
#endif

#endif /* __AIC_SELFTEST_H__ */
