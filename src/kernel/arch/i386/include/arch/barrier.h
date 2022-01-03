#ifndef ARCH_BARRIER_H
#define ARCH_BARRIER_H

/* compiler barrier */
#define barrier() asm volatile("": : :"memory")

#define mb()	asm volatile ("mfence" ::: "memory")
#define wmb()	asm volatile ("sfence" ::: "memory")
#define rmb()	asm volatile ("lfence" ::: "memory")

#endif