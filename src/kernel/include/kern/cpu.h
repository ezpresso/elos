#ifndef KERN_CPU_H
#define KERN_CPU_H

#include <arch/cpu.h>

#define cpu_from_arch(ptr) container_of(ptr, cpu_t, arch)

#define next_ap(cur) \
	(cur) == &boot_cpu ? (cur)->next : (cur)

#define foreach_ap(cur) \
	for((cur) = next_ap(cpu_list); (cur) != NULL; \
		(cur) = next_ap((cur)->next))

#define foreach_cpu(cur) \
	for((cur) = cpu_list; (cur) != NULL; (cur) = (cur)->next)

typedef struct cpu {
	/*
	 * TODO Consider putting arch entirely into
	 * PERCPU
	 */
	arch_cpu_t arch;

	struct cpu *next;
	struct thread *boot_thr;
	bool running;

	/*
	 * meaning is arch-specific (e.g. lapic id for x86)
	 * TODO
	 */
	int id;

	/*
	 * The virtual address space thats currently active on the cpu
	 */
	struct vm_vas *vm_vas;
	void *percpu;
} cpu_t;

extern cpu_t *cpu_list;
extern cpu_t boot_cpu;

static inline bool bsp_p(void) {
	return cur_cpu() == &boot_cpu;
}

size_t cpu_num(void);
void cpu_register(int id, bool is_boot);

#endif