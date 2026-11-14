/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#define CLK_VE 1
uint32_t readl(uintptr_t address);
void writel(uint32_t value, uintptr_t address);
int hal_clk_set_freq(uint32_t id, unsigned long frequency);
int hal_clk_enable(uint32_t id);
int hal_clk_disable(uint32_t id);
void aicos_dma_sync(void);
