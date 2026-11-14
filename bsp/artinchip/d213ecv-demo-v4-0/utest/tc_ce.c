/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * D213 Crypto Engine (CE) regression through the RT-Thread hwcrypto
 * framework.  The vectors are the published FIPS-197 / NIST / RFC
 * known answers, so the suite proves both the HAL programming and the
 * framework adapter without any external equipment.
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "utest.h"

#if defined(BSP_USING_CE)

#include "aic_hal_clk.h"
#include "aic_clk_id.h"
#include "hwcrypto.h"
#include "hw_hash.h"
#include "hw_symmetric.h"

static struct rt_hwcrypto_device *ce_dev;

static void ce_hash_test(hwcrypto_type type, const rt_uint8_t *in,
                         rt_size_t len, const rt_uint8_t *expect,
                         rt_size_t digest_len, rt_size_t split)
{
    struct rt_hwcrypto_ctx *ctx;
    rt_uint8_t out[64];
    rt_err_t err;

    ctx = rt_hwcrypto_hash_create(ce_dev, type);
    uassert_not_null(ctx);
    if (ctx == RT_NULL)
        return;

    if (split > 0 && split < len)
    {
        err = rt_hwcrypto_hash_update(ctx, in, split);
        uassert_int_equal(err, RT_EOK);
        in += split;
        len -= split;
    }

    err = rt_hwcrypto_hash_update(ctx, in, len);
    uassert_int_equal(err, RT_EOK);

    rt_memset(out, 0, sizeof(out));
    err = rt_hwcrypto_hash_finish(ctx, out, digest_len);
    uassert_int_equal(err, RT_EOK);
    uassert_buf_equal(out, expect, digest_len);

    rt_hwcrypto_hash_destroy(ctx);
}

static void ce_aes_ecb_test(const rt_uint8_t *key, rt_uint32_t keybits,
                            const rt_uint8_t *plain,
                            const rt_uint8_t *expect_cipher)
{
    struct rt_hwcrypto_ctx *ctx;
    rt_uint8_t cipher[16];
    rt_uint8_t back[16];

    ctx = rt_hwcrypto_symmetric_create(ce_dev, HWCRYPTO_TYPE_AES_ECB);
    uassert_not_null(ctx);
    if (ctx == RT_NULL)
        return;

    uassert_int_equal(rt_hwcrypto_symmetric_setkey(ctx, key, keybits),
                      RT_EOK);
    uassert_int_equal(rt_hwcrypto_symmetric_crypt(ctx,
                      HWCRYPTO_MODE_ENCRYPT, 16, plain, cipher), RT_EOK);
    uassert_buf_equal(cipher, expect_cipher, 16);
    uassert_int_equal(rt_hwcrypto_symmetric_crypt(ctx,
                      HWCRYPTO_MODE_DECRYPT, 16, cipher, back), RT_EOK);
    uassert_buf_equal(back, plain, 16);

    rt_hwcrypto_symmetric_destroy(ctx);
}

static void ce_aes_cbc_test(const rt_uint8_t *key, rt_uint32_t keybits,
                            const rt_uint8_t *iv, const rt_uint8_t *plain,
                            const rt_uint8_t *expect_cipher)
{
    struct rt_hwcrypto_ctx *ctx;
    rt_uint8_t cipher[16];
    rt_uint8_t back[16];

    ctx = rt_hwcrypto_symmetric_create(ce_dev, HWCRYPTO_TYPE_AES_CBC);
    uassert_not_null(ctx);
    if (ctx == RT_NULL)
        return;

    uassert_int_equal(rt_hwcrypto_symmetric_setkey(ctx, key, keybits),
                      RT_EOK);
    uassert_int_equal(rt_hwcrypto_symmetric_setiv(ctx, iv, 16), RT_EOK);
    uassert_int_equal(rt_hwcrypto_symmetric_crypt(ctx,
                      HWCRYPTO_MODE_ENCRYPT, 16, plain, cipher), RT_EOK);
    uassert_buf_equal(cipher, expect_cipher, 16);

    /* The engine leaves the IV on the last ciphertext block. */
    uassert_int_equal(rt_hwcrypto_symmetric_setiv(ctx, iv, 16), RT_EOK);
    uassert_int_equal(rt_hwcrypto_symmetric_crypt(ctx,
                      HWCRYPTO_MODE_DECRYPT, 16, cipher, back), RT_EOK);
    uassert_buf_equal(back, plain, 16);

    rt_hwcrypto_symmetric_destroy(ctx);
}

static void tc_ce_sha256(void)
{
    static const rt_uint8_t abc[] = { 'a', 'b', 'c' };
    static const rt_uint8_t abc_digest[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
    };
    static const rt_uint8_t msg[] =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    static const rt_uint8_t msg_digest[32] = {
        0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8,
        0xe5, 0xc0, 0x26, 0x93, 0x0c, 0x3e, 0x60, 0x39,
        0xa3, 0x3c, 0xe4, 0x59, 0x64, 0xff, 0x21, 0x67,
        0xf6, 0xec, 0xed, 0xd4, 0x19, 0xdb, 0x06, 0xc1
    };

    ce_hash_test(HWCRYPTO_TYPE_SHA256, abc, sizeof(abc), abc_digest,
                 sizeof(abc_digest), 0);
    /* The same message, delivered in two updates (56 + 0 split at 20). */
    ce_hash_test(HWCRYPTO_TYPE_SHA256, msg, sizeof(msg) - 1, msg_digest,
                 sizeof(msg_digest), 20);
}

static void tc_ce_sha1_md5(void)
{
    static const rt_uint8_t abc[] = { 'a', 'b', 'c' };
    static const rt_uint8_t sha1_digest[20] = {
        0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a,
        0xba, 0x3e, 0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c,
        0x9c, 0xd0, 0xd8, 0x9d
    };
    static const rt_uint8_t md5_digest[16] = {
        0x90, 0x01, 0x50, 0x98, 0x3c, 0xd2, 0x4f, 0xb0,
        0xd6, 0x96, 0x3f, 0x7d, 0x28, 0xe1, 0x7f, 0x72
    };

    ce_hash_test(HWCRYPTO_TYPE_SHA1, abc, sizeof(abc), sha1_digest,
                 sizeof(sha1_digest), 0);
    ce_hash_test(HWCRYPTO_TYPE_MD5, abc, sizeof(abc), md5_digest,
                 sizeof(md5_digest), 1);
}

static void tc_ce_aes_ecb(void)
{
    static rt_uint8_t block_in[64] __attribute__((aligned(64)));
    static rt_uint8_t block_out[64] __attribute__((aligned(64)));
    static rt_uint8_t block_expect[64];
    struct rt_hwcrypto_ctx *ctx;
    int i;

    static const rt_uint8_t key128[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    static const rt_uint8_t key256[32] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
    };
    static const rt_uint8_t plain[16] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
    };
    static const rt_uint8_t cipher128[16] = {
        0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
        0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a
    };
    static const rt_uint8_t cipher256[16] = {
        0x8e, 0xa2, 0xb7, 0xca, 0x51, 0x67, 0x45, 0xbf,
        0xea, 0xfc, 0x49, 0x90, 0x4b, 0x49, 0x60, 0x89
    };

    ce_aes_ecb_test(key128, 128, plain, cipher128);
    ce_aes_ecb_test(key256, 256, plain, cipher256);

    /*
     * 64-byte aligned buffers take the driver's direct-DMA path
     * instead of the bounce buffer, so this covers the cache
     * maintenance on both the input and the output.
     */
    for (i = 0; i < 4; i++)
    {
        rt_memcpy(block_in + i * 16, plain, 16);
        rt_memcpy(block_expect + i * 16, cipher128, 16);
    }

    ctx = rt_hwcrypto_symmetric_create(ce_dev, HWCRYPTO_TYPE_AES_ECB);
    uassert_not_null(ctx);
    if (ctx != RT_NULL)
    {
        uassert_int_equal(rt_hwcrypto_symmetric_setkey(ctx, key128, 128),
                          RT_EOK);
        uassert_int_equal(rt_hwcrypto_symmetric_crypt(ctx,
                          HWCRYPTO_MODE_ENCRYPT, sizeof(block_in),
                          block_in, block_out), RT_EOK);
        uassert_buf_equal(block_out, block_expect, sizeof(block_expect));
        rt_hwcrypto_symmetric_destroy(ctx);
    }
}

static void tc_ce_aes_cbc(void)
{
    static const rt_uint8_t key[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    static const rt_uint8_t iv[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    static const rt_uint8_t plain[16] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a
    };
    static const rt_uint8_t cipher[16] = {
        0x76, 0x49, 0xab, 0xac, 0x81, 0x19, 0xb2, 0x46,
        0xce, 0xe9, 0x8e, 0x9b, 0x12, 0xe9, 0x19, 0x7d
    };

    ce_aes_cbc_test(key, 128, iv, plain, cipher);
}

static void tc_ce_clock(void)
{
    struct rt_hwcrypto_ctx *ctx;

    /* Creating a context powers the engine through hal_crypto_init(). */
    ctx = rt_hwcrypto_hash_create(ce_dev, HWCRYPTO_TYPE_SHA256);
    uassert_not_null(ctx);
    uassert_true(hal_clk_is_enabled(CLK_CE));
    if (ctx != RT_NULL)
        rt_hwcrypto_hash_destroy(ctx);
}

static void testcase(void)
{
    ce_dev = rt_hwcrypto_dev_default();
    uassert_not_null(ce_dev);
    if (ce_dev == RT_NULL)
        return;

    rt_kprintf("tc_ce: hwcrypto device %s, id 0x%lx\n",
               ce_dev->parent.parent.name, (unsigned long)rt_hwcrypto_id(ce_dev));

    UTEST_UNIT_RUN(tc_ce_sha256);
    UTEST_UNIT_RUN(tc_ce_sha1_md5);
    UTEST_UNIT_RUN(tc_ce_aes_ecb);
    UTEST_UNIT_RUN(tc_ce_aes_cbc);
    UTEST_UNIT_RUN(tc_ce_clock);
}
UTEST_TC_EXPORT(testcase, "bsp.d213.ce", RT_NULL, RT_NULL, 30);

#endif /* BSP_USING_CE */
