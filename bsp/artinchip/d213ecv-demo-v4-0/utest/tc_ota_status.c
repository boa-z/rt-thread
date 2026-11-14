/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Read-only regression for the ArtInChip Bootloader A/B contract:
 * redundant env parsing and the os/os_r FIT headers.
 *
 * The test never writes env, os or os_r.  os_r may legitimately be
 * empty on a board that has never received an A/B update, so only the
 * running os slot must contain a valid FIT image.
 */

#include <rtthread.h>

#include "utest.h"

#if defined(BSP_USING_SPINAND_MTD_NAND)

#include <rtdevice.h>
#include <stdlib.h>
#include <string.h>

#include "drv_ota_slot.h"
#include "drv_spinand.h"

static void tc_ota_env(void)
{
    struct aic_ota_env_status st;
    char val[32];

    uassert_int_equal(aic_ota_env_status(&st), RT_EOK);
    uassert_true(st.active >= 0);
    uassert_true(st.active < AIC_OTA_ENV_COPIES);
    uassert_true(st.valid[st.active]);

    uassert_int_equal(aic_ota_env_get("osAB_now", val, sizeof(val)), RT_EOK);
    uassert_true(strcmp(val, "A") == 0 || strcmp(val, "B") == 0);
    rt_kprintf("tc_ota: osAB_now=%s from %s\n", val,
               st.active == 0 ? "env" : "env_r");

    uassert_int_equal(aic_ota_env_get("osAB_next", val, sizeof(val)), RT_EOK);
    uassert_true(strcmp(val, "A") == 0 || strcmp(val, "B") == 0);
    rt_kprintf("tc_ota: osAB_next=%s\n", val);

    uassert_int_equal(aic_ota_env_get("upgrade_available", val, sizeof(val)),
                      RT_EOK);
    uassert_true(strcmp(val, "0") == 0 || strcmp(val, "1") == 0);

    uassert_int_equal(aic_ota_env_get("bootlimit", val, sizeof(val)), RT_EOK);
    uassert_true(atoi(val) > 0);

    uassert_int_equal(aic_ota_env_get("bootcount", val, sizeof(val)), RT_EOK);
    uassert_true(atoi(val) >= 0);
}

static void tc_ota_slot(void)
{
    struct aic_nand_fit_info fit;
    char val[32];

    uassert_int_equal(aic_nand_get_fit_info("os", &fit), RT_EOK);
    uassert_true(fit.read_err == RT_MTD_EOK ||
                 fit.read_err == RT_MTD_EECC_CORRECT);
    uassert_true(fit.present);
    rt_kprintf("tc_ota: os FIT totalsize %u, partition %u bytes\n",
               fit.totalsize, fit.part_bytes);

    uassert_int_equal(aic_nand_get_fit_info("os_r", &fit), RT_EOK);
    uassert_true(fit.read_err == RT_MTD_EOK ||
                 fit.read_err == RT_MTD_EECC_CORRECT);
    rt_kprintf("tc_ota: os_r %s\n",
               fit.present ? "has a FIT image" : "is empty");

    uassert_int_equal(aic_ota_env_get("osAB_now", val, sizeof(val)), RT_EOK);
    rt_kprintf("tc_ota: running from osAB_now=%s (%s)\n", val,
               strcmp(val, "A") == 0 ? "os" : "os_r");
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_ota_env);
    UTEST_UNIT_RUN(tc_ota_slot);
}
UTEST_TC_EXPORT(testcase, "bsp.d213.ota", RT_NULL, RT_NULL, 60);

#endif /* BSP_USING_SPINAND_MTD_NAND */
