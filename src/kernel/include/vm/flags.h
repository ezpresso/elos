#ifndef VM_FLAGS_H
#define VM_FLAGS_H

/* common flags */
#define VM_NOFLAG		0
#define VM_PROT_RW		(VM_PROT_WR | VM_PROT_RD)
#define VM_PROT_RWX		(VM_PROT_WR | VM_PROT_RD | VM_PROT_EXEC)
#define VM_PROT_MASK		(VM_PROT_RWX | VM_PROT_KERN)
#define VM_PROT_NONE		0
#define VM_PROT_WR		(1 << 0)
#define VM_PROT_RD		(1 << 1)
#define VM_PROT_EXEC		(1 << 2)
#define VM_ZERO			(1 << 3)
#define VM_PROT_KERN		(1 << 4)
#define VM_PROT_USER		(0)
#define VM_WAIT			(1 << 5)
#define VM_NOWAIT		(0)

/* usage specific flags */
#define VM_FLAG1		(1 << 6) /* VM_SLAB_NOVALLOC or VM_MAP_SHARED */
#define VM_FLAG2		(1 << 7) /* VM_MAP_FIXED */
#define VM_FLAG3		(1 << 8) /* VM_MAP_PGOUT */
#define VM_FLAG4		(1 << 9) /* VM_MAP_32 */
#define VM_FLAG5		(1 << 10) /* VM_MAP_SHADOW */
#define VM_FLAG6		(1 << 11)

#define VM_FLAGS_PROT(f)	((f) & VM_PROT_MASK)

/* some predicates */
#define VM_PROT_KERN_P(f)	!!((f) & VM_PROT_KERN)
#define VM_PROT_USER_P(f)	!((f) & VM_PROT_KERN)
#define VM_PROT_WR_P(f)		!!((f) & VM_PROT_WR)
#define VM_PROT_RD_P(f)		!!((f) & VM_PROT_RD)
#define VM_PROT_RW_P(f)		(((f) & VM_PROT_RW) == VM_PROT_RW)
#define VM_PROT_RO_P(f)		(((f) & VM_PROT_RW) == VM_PROT_RD)
#define VM_PROT_EXEC_P(f)	!!((f) & VM_PROT_EXEC)
#define VM_WAIT_P(f)		!!((f) & VM_WAIT)
#define VM_ZERO_P(f)		!!((f) & VM_ZERO)

/**
 * @brief Panic if @p flags contains flags other than @p supported.
 */
#define VM_FLAGS_CHECK(flags, supported) \
	kassert(((flags) & ~(supported)) == 0, "[vm] unsupported flags: %u", \
		(flags) & ~(supported));

/* TODO split up into
 * vm_map_flags_t;
 * vm_prot_t;
 * vm_alloc_flags_t;
 */

typedef uint16_t vm_flags_t;

#include <arch/memattr.h>
typedef uint8_t vm_memattr_t;

#endif