/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * T-Head E9xx (E906/E907) RV32 CPU port for the D12p BSP.
 *
 * This port is kept inside the D126 BSP until the D12p/D13x CPU layer
 * has been validated on both SoC families and can be promoted to libcpu.
 * The interrupt controller is a CLIC, not a CLINT/PLIC pair.  Context
 * switching is triggered through the T-Head software interrupt pending
 * register, which is latched and serviced through CLIC vector 3.
 */

#ifndef CPUPORT_H__
#define CPUPORT_H__

#include <rtconfig.h>

/* Bytes of register width. */
#define STORE                   sw
#define LOAD                    lw
#define REGBYTES                4

#ifdef ARCH_RISCV_FPU
#define FSTORE                  fsw
#define FLOAD                   flw
#define FREGBYTES               4
#endif

/*
 * E9xx exposes the software interrupt pending register at
 * CPU_BASE + 0x80100c.  CPU_BASE is SoC specific.
 */
#ifndef CPU_BASE
#error "CPU_BASE must be defined by the SoC Kconfig"
#endif

#define AIC_TSPEND_ADDR         (CPU_BASE + 0x80100c)
#define AIC_SYNC_I              .long 0x01a0000b

#endif /* CPUPORT_H__ */
