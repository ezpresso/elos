#ifndef ARCH_CPU_H
#define ARCH_CPU_H

#include <arch/gdt.h>
#include <arch/x86.h>
#include <arch/stack.h>

struct trapframe;
struct cpu;

typedef enum cpu_vendor {
	CPU_VEN_INTEL = 0,
} cpu_vendor_t;

typedef struct arch_cpu {
	struct cpu *self; /* Needed for cpu-local storage */
	gdt_entry_t gdt[NGDT];
	tss_entry_t tss;
	stack_canary_t canary; /* loaded in gs segment */
} arch_cpu_t;

typedef void (cpu_intr_hand_t) (int intr, struct trapframe *, void *arg);

typedef struct cpu_intr {
	cpu_intr_hand_t *hand;
	void *arg;
} cpu_intr_t;

extern cpu_vendor_t cpu_vendor;
extern char cpu_model[64];
extern uint32_t cpu_id;
extern uint32_t cpu_procinfo;
extern uint32_t cpu_feature;
extern uint32_t cpu_featur2;
extern uint32_t cpuid_high;
extern uint32_t cpuid_exthigh;

#define cpu_local_get_atomic(var) ({	\
	uint32_t res;			\
	asm("mov %%fs:%p1, %0" : "=r" (res) :  "p" (offsetof(cpu_t, var))); \
	res;																\
})

#if 0
static inline struct thread *cur_thread(void) {
	/* Cannot simply use cur_cpu()->arch.thread, because the kernel is
	 * preemptive and cur_cpu() might change at any point in time
	 * (except when interrupts are disabled). That's why we atomically
	 * read %fs+(offsetof(arch.thread)) here. Caching %fs in a temporary
	 * register (as with cur_cpu()->arch.thread) would be erroneous
	 * (I'm actually not sure if the compiler would cache it or if it
	 * could optimize the statement).
	 *
	 * Quick note: currently cur_cpu never changes, because of dumb sched
	 * TODO
	 */
	return cpu_local_get_atomic(arch.thread);
}
#endif

static inline struct cpu *cur_cpu(void) {
	/* TODO kassert(!cpu_intr_enabled); */
#if 0
	/* does not work with clang: extern struct cpu *cpu asm("%fs:0"); */
#else
	struct cpu *cpu;
	asm("mov %%fs:0, %0" : "=r" (cpu));
#endif
	return cpu;
}

static inline void cpu_intr_set(bool on) {
	if(on) {
		sti();
	} else {
		cli();
	}
}

static inline bool cpu_intr_enabled(void) {
	return !!(eflags_get() & EFL_IF);
}

static inline void cpu_relax(void) {
	asm volatile ("pause");
}

static inline const char *cpu_vendor_str(cpu_vendor_t vendor) {
	kassert(vendor == CPU_VEN_INTEL, "[cpu] unknown cpu vendor: %d",
		vendor);
	return "Intel";
}

void cpu_set_kernel_stack(uintptr_t stack);
void cpu_set_intr_handler(uint8_t num, cpu_intr_hand_t *hand, void *arg);

void setgs(uint32_t base);
uint32_t getgs(void);

void __noreturn arch_cpu_idle(void);
void arch_cpu_init(arch_cpu_t *);

#endif
