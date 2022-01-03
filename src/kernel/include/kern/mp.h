#ifndef KERN_MP_H
#define KERN_MP_H

#include <config.h>

#if CONFIGURED(MP)

#include <arch/mp.h>
struct cpu;

extern bool ipi_enabled;

/**
 * Defined by arch.
 */
void ipi_bitmap_send(struct cpu *cpu);

void ipi_panic(void);
void ipi_bitmap_handler(void);
void ipi_preempt(struct cpu *cpu);
void ipi_intr(struct cpu *cpu);


bool mp_capable(void);
void mp_ap_wait(void);
void init_mp(void);
#else

static inline void ipi_panic(void) { }
static inline void ipi_preempt(struct cpu *cpu) { (void) cpu; }
static inline void ipi_intr(struct cpu *cpu) { (void) cpu; }
static inline bool mp_capable() { return false; }
static inline void init_mp() { }
#endif
#endif
