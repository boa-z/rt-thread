/* SPDX-License-Identifier: Apache-2.0
 * Exercise the production driver against a deterministic RT/MMIO test double.
 * Does not emulate codec execution or prove physical register/IRQ timing.
 */
#include <assert.h>
#include <string.h>
#include "drv_ve.c"

static rt_thread_t current = (void *)1;
static unsigned int regs[0x2100 / 4];
static int irq_nest;
static int clock_on, resets, fail_clock, fail_reset, fail_register, masked;
rt_thread_t rt_thread_self(void) { return current; }
int rt_interrupt_get_nest(void) { return irq_nest; }
int rt_tick_from_millisecond(int ms) { return ms; }
int rt_mutex_init(struct rt_mutex *m, const char *n, int f)
{ (void)n; (void)f; memset(m, 0, sizeof(*m)); return 0; }
int rt_mutex_detach(struct rt_mutex *m) { assert(!m->depth); return 0; }
int rt_mutex_take(struct rt_mutex *m, int timeout)
{
    (void)timeout;
    if (m->owner && m->owner != current) return -RT_ETIMEOUT;
    m->owner = current; m->depth++; return 0;
}
int rt_mutex_release(struct rt_mutex *m)
{
    assert(m->owner == current && m->depth > 0);
    if (!--m->depth) m->owner = 0;
    return 0;
}
int rt_sem_init(struct rt_semaphore *s, const char *n, int v, int f)
{ (void)n; (void)f; s->value = v; return 0; }
int rt_sem_detach(struct rt_semaphore *s) { (void)s; return 0; }
int rt_sem_take(struct rt_semaphore *s, int timeout)
{ (void)timeout; if (!s->value) return -RT_ETIMEOUT; s->value--; return 0; }
int rt_sem_release(struct rt_semaphore *s) { s->value++; return 0; }
int rt_sem_control(struct rt_semaphore *s, int cmd, void *arg)
{ assert(cmd == RT_IPC_CMD_RESET && !arg); s->value = 0; return 0; }
rt_base_t rt_hw_interrupt_disable(void) { return 0; }
void rt_hw_interrupt_enable(rt_base_t level) { (void)level; }
void rt_hw_interrupt_mask(int irq) { assert(irq == VE_IRQ); masked = 1; }
void rt_hw_interrupt_umask(int irq) { assert(irq == VE_IRQ); masked = 0; }
void rt_hw_interrupt_install(int irq, void (*isr)(int, void *), void *arg, const char *name)
{ assert(irq == VE_IRQ && isr == ve_isr && arg == &ve); (void)name; }
int rt_device_register(rt_device_t dev, const char *name, int flags)
{ assert(dev == &ve.dev && !strcmp(name, "ve") && (flags & RT_DEVICE_FLAG_STANDALONE)); return fail_register; }
uint32_t readl(uintptr_t address)
{ assert(address >= VE_BASE && address - VE_BASE < sizeof(regs)); return regs[(address - VE_BASE) / 4]; }
void writel(uint32_t value, uintptr_t address)
{
    assert(clock_on);
    assert(address >= VE_BASE && address - VE_BASE < sizeof(regs));
    unsigned int off = address - VE_BASE;
    if (off == 0x128 || off == 0x2004 || off == 0xc04) regs[off / 4] &= ~value;
    else regs[off / 4] = value;
}
int hal_clk_set_freq(uint32_t id, unsigned long hz)
{ assert(id == CLK_VE && hz == VE_CLOCK); return fail_clock; }
int hal_clk_enable(uint32_t id) { assert(id == CLK_VE); clock_on = 1; return 0; }
int hal_clk_disable(uint32_t id) { assert(id == CLK_VE); clock_on = 0; return 0; }
int hal_reset_assert(uint32_t id)
{ assert(id == RESET_VE); resets++; memset(regs, 0, sizeof(regs)); return 0; }
int hal_reset_deassert(uint32_t id) { assert(id == RESET_VE); return fail_reset; }
void aicos_dma_sync(void) {}
int main(void)
{
    struct wait_info wait = {2000, 0};
    struct aic_ve_info info;
    assert(!drv_ve_open());
    fail_register = -1; assert(rt_hw_ve_init() != 0); fail_register = 0;
    assert(!rt_hw_ve_init());
    fail_clock = -1; assert(!drv_ve_open() && !clock_on); fail_clock = 0;
    fail_reset = -1; assert(!drv_ve_open() && !clock_on && masked); fail_reset = 0;
    struct aic_ve_client *v = drv_ve_open();
    assert(v && clock_on && !masked);
    assert(!drv_ve_open());
    assert(!drv_ve_control(v, IOC_VE_GET_INFO, &info));
    assert(info.reg_base == VE_BASE && info.timeout_ms == 2000);
    assert(!drv_ve_control(v, IOC_VE_GET_CLIENT, NULL));
    assert(drv_ve_control(v, IOC_VE_GET_CLIENT, NULL) == -RT_EBUSY);
    assert(drv_ve_close(v) == -RT_EBUSY);
    current = (void *)2;
    assert(drv_ve_control(v, IOC_VE_GET_CLIENT, NULL) == -RT_ETIMEOUT);
    assert(drv_ve_control(v, IOC_VE_WAIT, &wait) == -RT_EPERM);
    assert(drv_ve_control(v, IOC_VE_PUT_CLIENT, NULL) == -RT_EPERM);
    assert(drv_ve_close(v) == -RT_ETIMEOUT);
    current = (void *)1;
    int before = resets;
    assert(drv_ve_control(v, IOC_VE_WAIT, &wait) == -RT_ETIMEOUT);
    assert(resets > before && !wait.reg_status && !ve.done.value);
    regs[0x14 / 4] = 1; regs[0x2004 / 4] = 1;
    ve_isr(VE_IRQ, v); ve_isr(VE_IRQ, v);
    assert(ve.done.value == 1); /* duplicate PLIC delivery ignored */
    assert(!drv_ve_control(v, IOC_VE_WAIT, &wait) && wait.reg_status == 1);
    before = resets;
    assert(!drv_ve_control(v, IOC_VE_PUT_CLIENT, NULL));
    assert(resets == before); /* completed codec state survives PUT */
    assert(!drv_ve_control(v, IOC_VE_GET_CLIENT, NULL));
    wait.wait_time = 2001;
    assert(drv_ve_control(v, IOC_VE_WAIT, &wait) == -RT_EINVAL);
    assert(!drv_ve_control(v, IOC_VE_PUT_CLIENT, NULL));
    assert(resets > before); /* no completed WAIT: PUT aborts DMA */
    assert(!drv_ve_close(v) && !clock_on && masked);
    assert(drv_ve_control(v, IOC_VE_GET_CLIENT, NULL) == -RT_EIO);
    irq_nest = 1;
    assert(!drv_ve_open());
    irq_nest = 0;
    assert(!ve.dev.ops->open(&ve.dev, 0));
    assert(!drv_ve_control(v, IOC_VE_GET_CLIENT, NULL));
    ve.dev.ref_count = 0; /* model core's pre-close decrement */
    assert(ve.dev.ops->close(&ve.dev) == -RT_EBUSY);
    assert(ve.dev.ref_count == 1);
    assert(!drv_ve_control(v, IOC_VE_PUT_CLIENT, NULL));
    ve.dev.ref_count--;
    assert(!ve.dev.ops->close(&ve.dev));
    return 0;
}
