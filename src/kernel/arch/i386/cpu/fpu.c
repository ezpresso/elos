/*
 * ███████╗██╗      ██████╗ ███████╗
 * ██╔════╝██║     ██╔═══██╗██╔════╝
 * █████╗  ██║     ██║   ██║███████╗
 * ██╔══╝  ██║     ██║   ██║╚════██║
 * ███████╗███████╗╚██████╔╝███████║
 * ╚══════╝╚══════╝ ╚═════╝ ╚══════╝
 *
 * Copyright (c) 2017, Elias Zell
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <kern/system.h>
#include <kern/fault.h>
#include <kern/user.h>
#include <kern/proc.h>
#include <lib/string.h>
#include <arch/fpu.h>
#include <arch/x86.h>

static fpstate_t fpu_default __align(FPU_ALIGN);
static bool fpu_default_init = false;

void fpu_cpu_init(void) {
	/* TODO check whether the CPU supports fxsave */

	cr4_set(cr4_get() | (CR4_OSXMMEXCPT | CR4_OSFXSR));

	/*
	 * Clear CR0.EM, CR0.TS, set CR0.MP and set CR0.NE.
	 */
	cr0_set((cr0_get() & ~(CR0_EM | CR0_TS)) | CR0_MP | CR0_NE);

	fninit();
	fldcw(0x37F);

	if(!fpu_default_init) {
		fpu_default_init = true;
		fpu_save(&fpu_default);
	}
}

void fpu_init(arch_thread_t *thread) {
	thread->fpu = ALIGN_PTR(&thread->fpubuf, FPU_ALIGN);
	fpu_clone(thread->fpu, &fpu_default);
}

void fpu_clone(fpstate_t *dst, fpstate_t *src) {
	memcpy(dst, src, FPU_REGS_SZ);
}

void fpu_save(fpstate_t *fpu) {
	fxsave(fpu);
}

void fpu_restore(fpstate_t *fpu) {
	fxrstor(fpu);
}

int copyout_fpu(fpstate_t *fp) {
	if(!PTR_ALIGNED(fp, FPU_ALIGN)) {
		goto error;
	}

	if(user_io_check(fp, sizeof(*fp), NULL) == 0) {
		mayfault(error) {
			fxsave(fp);
		}
	}

	return 0;

error:
	return -EFAULT;
}

int copyin_fpu(fpstate_t *fp) {
	if(!PTR_ALIGNED(fp, FPU_ALIGN)) {
		goto error;
	}

	if(user_io_check(fp, sizeof(*fp), NULL) == 0) {
		mayfault(error) {
			fxrstor(fp);
		}

		return 0;
	}

error:
	return -EFAULT;
}
