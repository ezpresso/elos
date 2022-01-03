#ifndef ARCH_MTRR_H
#define ARCH_MTRR_H

#define MTRR_CACHE_UC	0x0	/* uncachable */
#define MTRR_CACHE_WC	0x1	/* Write Combining */
#define MTRR_CACHE_WT	0x4	/* Write-through */
#define MTRR_CACHE_WP	0x5	/* Write-protected */
#define MTRR_CACHE_WB	0x6	/* Writeback */

typedef int mtrr_t;
int mtrr_range_add(mtrr_t *mtrr, uintptr_t addr, uintptr_t len, uint32_t type);

#endif
