#ifndef KERN_PERCPU_H
#define KERN_PERCPU_H

#include <kern/section.h>
#include <kern/cpu.h>
 
/*
 * TODO replace some macros if non-smp mode was configured
 */
#define PERCPU_SECTION section(percpu, void *)

#define PERCPU_OFFSET(var) ({						       \
	size_t _pcpu_off;						       \
	_pcpu_off = (((void *)(var)) - (void *)section_first(PERCPU_SECTION)); \
	_pcpu_off;							       \
})

/**
 * @brief Get the pointer to a percpu vartiable.
 */
#define PERCPU(var) \
	PERCPU_CPU(cur_cpu(), var)

/**
 * @brief Get the pointer to a percpu vartiable for a cpi.
 */
#define PERCPU_CPU(cpu, var) \
	(typeof(var))((cpu)->percpu + PERCPU_OFFSET(var))

/**
 * @brief An attribute used to describe a per-cpu variable.
 */
#define __percpu section_entry(PERCPU_SECTION)

/**
 * @brief Used when defining a percpu variable.
 */
#define DEFINE_PERCPU(type, var) __percpu type var

#endif