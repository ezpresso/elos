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
#include <kern/setjmp.h>
#include <arch/frame.h>
#include <arch/x86.h>
#include <arch/gdt.h>

void tf_fake(trapframe_t *tf, uintptr_t ip, uintptr_t sp, bool usr) {
	int csel, dsel, tls;

	if(usr) {
		csel = UCODE_SEL;
		dsel = UDATA_SEL;
		tls = GS_SEL;
	} else {
		csel = KCODE_SEL;
		dsel = KDATA_SEL;
		tls = CANARY_SEL;
	}

	tf->fs = FS_SEL;
	tf->gs = tls;
	tf->cs = csel;
	tf->es = dsel;
	tf->ds = dsel;
	tf->ss = dsel;
	
	tf->edi = 0;
	tf->esi = 0;
	tf->ebp = 0;
	tf->esp = 0; /* kernel esp */
	tf->ebx = 0;
	tf->edx = 0;
	tf->ecx = 0;
	tf->eax = 0;

	tf->int_no = 0;
	tf->err_code = 0;

	tf->eip = ip;
	tf->eflags = EFL_IF;
	tf->useresp = sp;
}

void tf_set_jmp_buf(trapframe_t *tf, jmp_buf_t *buf) {
	tf->ebx = buf->ebx;
	tf->esi = buf->esi;
	tf->edi = buf->edi;
	tf->ebp = buf->ebp;
	tf->esp = buf->esp;
	tf->eip = buf->eip;
}
