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
#include <kern/symbol.h>

typedef struct src_location {
	const char *file;
	uint32_t line;
	uint32_t column;
} src_location_t;

typedef struct type_desc {
#define TK_INTEGER	0x0000
#define TK_FLOAT	0x0001
#define TK_UNKNOWN	0xffff
	uint16_t type_kind;
	uint16_t type_info;
	char type_name[1];
} type_desc_t;

typedef struct type_mismatch_data {
	src_location_t location;
	type_desc_t *type;
	uintptr_t alignment;
	unsigned char type_check_kind;
} type_mismatch_data_t;

typedef struct overflow_data {
	src_location_t location;
	type_desc_t *type;
} overflow_data_t;

typedef struct shift_out_of_bounds {
  src_location_t location;
  type_desc_t *lhs_type;
  type_desc_t *rhs_type;
} shift_out_of_bounds_t;

typedef struct unreachable_data {
	src_location_t location;
} unreachable_data_t;

typedef struct nonnull_arg_data {
	src_location_t location;
	src_location_t attr_loc;
	int idx;
} nonnull_arg_data_t;

typedef struct out_of_bounds_data {
	src_location_t location;
	type_desc_t *array_type;
	type_desc_t *index_type;
} out_of_bounds_data_t;

typedef struct vla_bound_data {
	src_location_t location;
	type_desc_t *type;
} vla_bound_data_t;

typedef struct invalid_value_data {
	src_location_t location;
	type_desc_t *type;
} invalid_value_data_t;

static const char *type_check_kinds[] = {
	"load of",
	"store to",
	"reference binding to",
	"member access within",
	"member call on",
	"constructor call on",
	"downcast of",
	"downcast of",
	"upcast of",
	"cast to virtual base of"
};

static void ubsan_log_start(char *name, src_location_t *location) {
	kprintf("[ubsan] %s at %s:%d:%d\n", name, location->file,
		location->line, location->column);
}

static void ubsan_printval(type_desc_t *type, uintptr_t val) {
	if(type->type_kind == TK_INTEGER) {
		if(type->type_info & 1) {
			kprintf("%d", val);
		} else {
			kprintf("%u", val);
		}
	} else if(type->type_kind == TK_FLOAT) {
		/* union {
			uintptr_t val;
			float f;
		} u = { val: val };
		kprintf("%f", u.f); */
		kprintf("(not printing float)");
	} else {
		kprintf("unknown");
	}
}

static bool ubsan_type_is_minus1(type_desc_t *type, uintptr_t val) {
	return type->type_kind == TK_INTEGER && type->type_info & 1 &&
		(intptr_t)val == -1;
}

static void ubsan_overflow(overflow_data_t *data, uintptr_t lhs, uintptr_t rhs,
	char op)
{
	const bool is_signed = data->type->type_info & 1;

	ubsan_log_start("overflow", &data->location);

	kprintf("\t%s integer overflow: ", is_signed ? "signed" : "unsigned");
	ubsan_printval(data->type, lhs);
	kprintf(" %c ", op);
	ubsan_printval(data->type, rhs);

	kprintf(" cannot be represented in type %s\n", data->type->type_name);
}

void __ubsan_handle_type_mismatch(type_mismatch_data_t *data, uintptr_t ptr) {
	ubsan_log_start("type mismatch", &data->location);

	if(ptr == 0) {
		kprintf("\t%s null pointer of type %s\n",
			type_check_kinds[data->type_check_kind],
			data->type->type_name);
	} else if(data->alignment && !ALIGNED(ptr, data->alignment)) {
		kprintf("\t%s misaligned address 0x%x for type %s, which "
			"requires %d byte alignment\n",
			type_check_kinds[data->type_check_kind],
			ptr,
			data->type->type_name,
			data->alignment);
	} else {
		kprintf("\t%s address 0x%x with insufficient space for an "
			"object of type %s\n",
			type_check_kinds[data->type_check_kind],
			ptr,
			data->type->type_name);
	}
}
export(__ubsan_handle_type_mismatch);

void __ubsan_handle_sub_overflow(overflow_data_t *data, uintptr_t lhs,
	uintptr_t rhs)
{
	ubsan_overflow(data, lhs, rhs, '-');
}
export(__ubsan_handle_sub_overflow);

void __ubsan_handle_add_overflow(overflow_data_t *data, uintptr_t lhs,
	uintptr_t rhs)
{
	ubsan_overflow(data, lhs, rhs, '+');
}
export(__ubsan_handle_add_overflow);

void __ubsan_handle_mul_overflow(overflow_data_t *data, uintptr_t lhs,
	uintptr_t rhs)
{
	ubsan_overflow(data, lhs, rhs, '*');
}
export(__ubsan_handle_mul_overflow);

void __ubsan_handle_divrem_overflow(overflow_data_t *data, uintptr_t lhs,
	uintptr_t rhs)
{
	ubsan_log_start("divrem overflow", &data->location);

	if(ubsan_type_is_minus1(data->type, rhs)) {
		kprintf("\tdivision of %d by -1 cannot be represented in "
			"type %s\n", lhs, data->type->type_name);
	} else {
		kprintf("\tdivision by 0\n");
	}
}
export(__ubsan_handle_divrem_overflow);

void __ubsan_handle_negate_overflow(overflow_data_t *data, uintptr_t val) {
	ubsan_log_start("negate overflow", &data->location);
	kprintf("\tcannot negate ");
	ubsan_printval(data->type, val);
	kprintf(" of type %s\n", data->type->type_name);
}
export(__ubsan_handle_negate_overflow);

void __ubsan_handle_shift_out_of_bounds(shift_out_of_bounds_t *data,
	uintptr_t lhs, uintptr_t rhs)
{
	ubsan_log_start("shift out of bounds", &data->location);

	kprintf("\ttried to shift ");
	ubsan_printval(data->lhs_type, lhs);
	kprintf(" by ");
	ubsan_printval(data->rhs_type, rhs);
	kprintf("\n");
}
export(__ubsan_handle_shift_out_of_bounds);

void __ubsan_handle_nonnull_arg(nonnull_arg_data_t *data) {
	ubsan_log_start("nonull argument", &data->location);
	kprintf("\tnull pointer passed as argument %d\n", data->idx);
}
export(__ubsan_handle_nonnull_arg);

void __ubsan_handle_builtin_unreachable(unreachable_data_t *data) {
	ubsan_log_start("unreachable statement", &data->location);
}
export(__ubsan_handle_builtin_unreachable);

void __ubsan_handle_out_of_bounds(out_of_bounds_data_t *data, uintptr_t index) {
	ubsan_log_start("out of bounds", &data->location);
	kprintf("\tindex %d for type %s\n", index, data->array_type->type_name);
}
export(__ubsan_handle_out_of_bounds);

void __ubsan_handle_vla_bound_not_positive(struct vla_bound_data *data,
	unsigned long bound)
{
	ubsan_log_start("variable length array bound value <= 0",
		&data->location);
	(void) bound;
}
export(__ubsan_handle_vla_bound_not_positive);

void __ubsan_handle_load_invalid_value(invalid_value_data_t *data,
	unsigned long val)
{
	ubsan_log_start("invalid value", &data->location);
	ubsan_printval(data->type, val);
}

#if notyet /* clang */
void __ubsan_handle_type_mismatch_v1(void) {
}
void __ubsan_handle_pointer_overflow(void) {
}
void __ubsan_handle_invalid_builtin(void) {
}
#endif

void __ubsan_handle_type_mismatch_v1(void) {
}

void __ubsan_handle_pointer_overflow(void) {
}

void __ubsan_handle_invalid_builtin(void) {
}
