
#if 0

#include <elos/system.h>
#include <elos/init.h>
#include <elos/cpu.h>
#include <elos/proc.h>
#include <arch/msr.h>
#include <arch/mtrr.h>
#include <arch/x86.h>
#include <elos/heap.h>

/* 64-ia-32-architecture-software-developer-manual
 * Vol. 3A 11.11 MEMORY TYPE RANGE REGISTERS (page 3046)
 */

#define LEN_TO_MASK(len)  \
	(~((len) - 1) & MTRR_PHYSBASE_MASK)

typedef struct mtrr_var_range {
#define MTRR_PHYSBASE_TYPE(x)	((x) & 0xff)
#define MTRR_PHYSBASE_MASK		(0xfffff000 /* 32 bit cpu */)
	uint64_t base;
	
#define MTRR_PHYMASK_VALID		(1 << 11)
	uint64_t mask;

	uint32_t ref;	
} mtrr_var_range_t;

typedef struct mtrr_fix_range {
	uint64_t types;
} mtrr_fix_range_t;

#define MTRR_CAP_VCNT(x)	((x) & 0xff)
#define MTRR_CAP_FIX		(1 << 8)
#define MTRR_CAP_WC			(1 << 10)
#define MTRR_CAP_SMRR		(1 << 11)
static uint64_t mtrr_cap;

#define MTRR_DEF_TYPE(x)	((x) & 0xff)
#define MTRR_DEF_FE			(1 << 10)
#define MTRR_DEF_E			(1 << 11)
static uint64_t mtrr_def_type;
static uint32_t mtrr_var_cnt;
static mtrr_fix_range_t mtrr_fix_range[11];
static mtrr_var_range_t *mtrr_var_range;
static bool mtrr_initialized = false;

static char *mtrr_type_to_str(uint8_t x) {
	switch(x) {
		case MTRR_CACHE_UC:
			return "uncachable";
		case MTRR_CACHE_WC:
			return "write-combining";
		case MTRR_CACHE_WT:
			return "write-through";
		case MTRR_CACHE_WP:
			return "write-protected";
		case MTRR_CACHE_WB:
			return "write-back";
		default:
			return "Unknown";
	}
}

int mtrr_range_add(mtrr_t *mtrr, uintptr_t addr, uintptr_t len, uint32_t type) {
	uint32_t i, /* cr4, */ cr0;
	
	if(!mtrr_initialized) {
		return -1;
	}
	
	if((type != MTRR_CACHE_UC) &&
		(type != MTRR_CACHE_WC) &&
		(type != MTRR_CACHE_WT) &&
		(type != MTRR_CACHE_WP) &&
		(type != MTRR_CACHE_WB))
	{
		return -1;
	}
	
	if(!IS_ALIGNED(addr, 0x1000) || !IS_ALIGNED(len, 0x1000)) {
		return -1;
	}
	
	if(type == MTRR_CACHE_WC && !(mtrr_cap & MTRR_CAP_WC)) {
		kprintf("[MTRR] Write-combining not supported\n");
		return -1;
	}
	
	if(!mtrr || addr < 0x100000 || !mtrr_var_cnt) {
		return -1;
	}
	
	for(i = 0; i < mtrr_var_cnt; i++) {
		if(!mtrr_var_range[i].ref) {
			kprintf("[MTRR] Found free var range: %d\n", i);
			mtrr_var_range[i].ref = 1;
			mtrr_var_range[i].base = (addr & MTRR_PHYSBASE_MASK) | MTRR_PHYSBASE_TYPE(type);
			mtrr_var_range[i].mask = LEN_TO_MASK(len) | MTRR_PHYMASK_VALID;
			*mtrr = i;
			break;
		}
	}
	
	pushcli();
	
	/* cr4 = cr4_get(); */
	cr0 = cr0_get();
	
	cr0_set((cr0 & ~CR0_NW) | CR0_CD);
	wbinvd();
	
	wrmsr64(MSR_IA32_MTRR_DEF_TYPE, mtrr_def_type & ~MTRR_DEF_E);	
	wrmsr64(MSR_IA32_MTRR_PHYSBASE(i), mtrr_var_range[i].base);
	wrmsr64(MSR_IA32_MTRR_PHYSMASK(i), mtrr_var_range[i].mask);
	wrmsr64(MSR_IA32_MTRR_DEF_TYPE, mtrr_def_type | MTRR_DEF_E);
	
	wbinvd();
	
	cr0_set(cr0);
	/* cr4_set(cr4); */
	
	popcli();
	
	return 0;
}

/* 
int __init mtrr_cpu_init(void) {
	
}
*/

static int __init mtrr_init(void) {
	uint32_t i;
	
	if(cpu_has_feature(CPU_FEATURE_MTRR)) {
		return INIT_ERR;
	}
	
	mtrr_cap = rdmsr64(MSR_IA32_MTRR_CAP);
	mtrr_def_type = rdmsr64(MSR_IA32_MTRR_DEF_TYPE);
	mtrr_var_cnt = MTRR_CAP_VCNT(mtrr_cap);
	
	kprintf("[MTRR] %d variable range MTRRS\n", mtrr_var_cnt);	
	if(mtrr_var_cnt) {
		mtrr_var_range = kcalloc(sizeof(*mtrr_var_range) * mtrr_var_cnt);
		if(!mtrr_var_range) {
			mtrr_var_cnt = 0;
		}
		
		/* Load the current var range configured by bios */
		for(i = 0; i < mtrr_var_cnt; i++) {
			mtrr_var_range[i].base = rdmsr64(MSR_IA32_MTRR_PHYSBASE(i));
			mtrr_var_range[i].mask = rdmsr64(MSR_IA32_MTRR_PHYSMASK(i));
			if(mtrr_var_range[i].mask & MTRR_PHYMASK_VALID) {
				kprintf("\t0x%llx: %s\n", mtrr_var_range[i].base & MTRR_PHYSBASE_MASK,
						mtrr_type_to_str(MTRR_PHYSBASE_TYPE(mtrr_var_range[i].base)));
						
				mtrr_var_range[i].ref = 1;
			}
		}
	}
	
	if(mtrr_cap & MTRR_CAP_FIX) {
		mtrr_fix_range[0].types = rdmsr64(MSR_IA32_MTRR_FIX64K_00000);
		mtrr_fix_range[1].types = rdmsr64(MSR_IA32_MTRR_FIX16K_80000);
		mtrr_fix_range[2].types = rdmsr64(MSR_IA32_MTRR_FIX16K_A0000);
		for(i = 0; i < 8; i++)
			mtrr_fix_range[3 + i].types = rdmsr64(MSR_IA32_MTRR_FIX4K_C0000 + i);
	}
		
	mtrr_initialized = true;
		
	return INIT_OK;
}

/* early_initcall(mtrr_init); */

#endif