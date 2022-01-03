#ifndef ARCH_MP_H
#define ARCH_MP_H

#include <config.h>

#define AP_CODE_ADDR 	0x7000
#define AP_ARG_TOP	(AP_CODE_ADDR + 0x1000)
#define AP_ARG_PGDIR	(AP_ARG_TOP - 0x10)
#define AP_ARG_STACK	(AP_ARG_TOP - 0xc)
#define AP_ARG_KMAIN	(AP_ARG_TOP - 0x8)
#define AP_ARG_CPU	(AP_ARG_TOP - 0x4)

#ifndef __ASSEMBLER__
struct mmu_ctx;

extern uint8_t ap_entry_start[];
extern uint8_t ap_entry_end[];

static inline void ap_tramp_arg(uintptr_t arg, uintptr_t value) {
	*(uint32_t *)arg = value;
}

#if CONFIGURED(MP)
bool mp_nmi_handler(void);
void ipi_invlpg(struct mmu_ctx *ctx, vm_vaddr_t addr, vm_vsize_t size);
#else
static inline void mp_nmi_handler(void) { return; }
static inline void ipi_invlpg(struct mmu_ctx *ctx, vm_vaddr_t addr,
	vm_vsize_t size)
{
	(void) ctx;
	(void) addr;
	(void) size;
}
#endif

void arch_mp_init(void);

#endif
#endif
