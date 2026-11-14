/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <limits.h>
#include <string.h>
#include "aic_buffer.h"

int main(void)
{
    struct aic_buffer_layout layout;
    struct mpp_buf b = {0};
    uint32_t address;
    assert(!aic_dma_address(0x40000000, 4096, &address) && address == 0x40000000);
#if UINTPTR_MAX > UINT32_MAX
    assert(aic_dma_address((uintptr_t)UINT32_MAX + 1, 1, &address) != 0);
#endif
    assert(aic_dma_address(UINT32_MAX, 2, &address) != 0);
    assert(aic_dma_address(1, SIZE_MAX, &address) != 0);
    assert(aic_dma_address(1, 0, &address) != 0);
    b.buf_type = MPP_PHY_ADDR;
    b.format = MPP_FMT_ARGB_8888; b.size.width = 64; b.size.height = 32;
    b.stride[0] = 256; b.phy_addr[0] = 0x40000000;
    assert(!aic_buffer_layout(&b, &layout));
    assert(layout.planes == 1 && layout.length[0] == 8192);
    b.size.width = INT_MAX; assert(aic_buffer_layout(&b, &layout) != 0); b.size.width = 64;
    b.stride[0] = UINT32_MAX; assert(aic_buffer_layout(&b, &layout) != 0); b.stride[0] = 256;
    b.phy_addr[0] = UINT32_MAX - 4095; assert(aic_buffer_layout(&b, &layout) != 0);
    b.phy_addr[0] = 0x40000000;
    b.crop_en = 1; b.crop.x = 63; b.crop.y = 0; b.crop.width = INT_MAX; b.crop.height = 1;
    assert(aic_buffer_layout(&b, &layout) != 0); b.crop_en = 0;
    b.format = MPP_FMT_NV12; b.stride[0] = b.stride[1] = 64; b.phy_addr[1] = 0x40002000;
    assert(!aic_buffer_layout(&b, &layout));
    assert(layout.planes == 2 && layout.length[0] == 2048 && layout.length[1] == 1024);
    b.size.height = 31; assert(aic_buffer_layout(&b, &layout) != 0); b.size.height = 32;
    b.format = MPP_FMT_YUV420P; b.stride[1] = b.stride[2] = 32; b.phy_addr[2] = 0x40003000;
    assert(!aic_buffer_layout(&b, &layout)); assert(layout.length[2] == 512);
    b.stride[2] = 64; assert(aic_buffer_layout(&b, &layout) != 0); b.stride[2] = 32;
    b.crop_en = 1; b.crop.x = 1; b.crop.width = 32; b.crop.height = 16;
    assert(aic_buffer_layout(&b, &layout) != 0); b.crop.x = 2;
    assert(!aic_buffer_layout(&b, &layout));
    b.format = MPP_FMT_YUV420_64x32_TILE; assert(aic_buffer_layout(&b, &layout) == -ENOTSUP);
    b.format = (enum mpp_pixel_format)-1; assert(aic_buffer_layout(&b, &layout) == -ENOTSUP);
    return 0;
}
