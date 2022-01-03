/*
 * ███████╗██╗      ██████╗ ███████╗
 * ██╔════╝██║     ██╔═══██╗██╔════╝
 * █████╗  ██║     ██║   ██║███████╗
 * ██╔══╝  ██║     ██║   ██║╚════██║
 * ███████╗███████╗╚██████╔╝███████║
 * ╚══════╝╚══════╝ ╚═════╝ ╚══════╝
 *
 * Copyright (c) 2018, Elias Zell
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
#include <vm/malloc.h>
#include <acpi/aml.h>
#include <acpi/amlpriv.h>

#define AML_OP(o, n, a) [AML_OP_ ## o] = { AML_OP_ ## o, n, a }
#define aml_panic(msg...) kpanic("[aml] " msg)

static size_t aml_depth = 0;

static aml_opcode_t aml_opcodes[256 /* TODO */] = {
	AML_OP(SCOPE, "Scope", "pnT")
};

static aml_scope_t *aml_pushscope(aml_scope_t *parent, aml_node_t *node) {
	aml_scope_t *scope;

	scope = kmalloc(sizeof(*scope), VM_WAIT);
	scope->parent = parent;
	scope->node = node;
	scope->ptr = node->start;

	return scope;
}

static void aml_popscope(aml_scope_t *scope) {
	kfree(scope);
}

static aml_op_t aml_next_op(aml_scope_t *scope) {
	aml_op_t op;

	op = aml_next(scope);
	switch(op) {
	case AML_OP_EXTPREFIX:
		return (op << 8) | aml_next(scope);
	default:
		return op;
	}
}

static aml_opcode_t *aml_opcode_lookup(aml_op_t op) {
	if(op & 0xFF00) {
		aml_panic("TODO extop: %d", op);
	}

	/*
	 * TEMPORARY
	 */
	aml_opcode_t *opcode = &aml_opcodes[op];
	if(opcode->name == NULL) {
		aml_panic("unkown opcode: %d", op);
	}

	return &aml_opcodes[op];
}

static void aml_parse_opcode_args(aml_scope_t *scope, aml_opcode_t *op,
	aml_value_t **args)
{
	const char *ptr;
	char arg;

	(void) args;
	(void) scope;

	ptr = op->args;
	while((arg = *ptr++) != '\0') {
		switch(arg) {
		case AML_ARG_NAME:
			break;
		}
	}
}

static void aml_parse_op(aml_scope_t *scope, aml_opcode_t *op) {
	aml_value_t *args[8];

	aml_parse_opcode_args(scope, op, args);
	kprintf("op: %d\n", op->op);
	for(;;) ;

	switch(op->op) {
	case AML_OP_SCOPE:
		break;
	default:
		break;
	}
}

static void aml_parse(aml_scope_t *scope) {
	aml_opcode_t *op;

	if(aml_depth++ > AML_DEPTH_MAX) {
		aml_panic("reached the maximum scope depth");
	}

	while(scope->ptr < aml_scope_end(scope)) {
		op = aml_opcode_lookup(aml_next_op(scope));
		aml_parse_op(scope, op);
	}
}

void aml_parse_dsdt(uint8_t *ptr, size_t length) {
	aml_scope_t *scope;

	tree_node_init(&aml_root, &aml_root.node);
	aml_root.start = ptr;
	aml_root.end = ptr + length;

	scope = aml_pushscope(NULL, &aml_root);
	aml_parse(scope);
	aml_popscope(scope);
}
