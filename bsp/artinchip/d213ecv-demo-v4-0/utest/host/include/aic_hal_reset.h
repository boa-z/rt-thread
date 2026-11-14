/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#define RESET_VE 1
int hal_reset_assert(uint32_t id);
int hal_reset_deassert(uint32_t id);
