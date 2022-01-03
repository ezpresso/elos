#ifndef ACPI_AML_H
#define ACPI_AML_H

#include <lib/tree.h>

/**
 * @brief The length of an AML node name.
 */
#define AML_NAMESZ 4

/**
 * @brief The maximun scope depth while parsing AML bytecode.
 */
#define AML_DEPTH_MAX 125

/**
 * @brief An AML opcode number.
 */
typedef uint16_t aml_op_t;

/**
 * @brief A structure describing an opcode and its arguments.
 */
typedef struct aml_opcode {
	aml_op_t op;
	const char *name;
	const char *args;
} aml_opcode_t;


/**
 * @brief The type of an AML value.
 */
typedef enum aml_value_type {
	AML_VALUE_INT,
} aml_value_type_t;

/**
 * @brief A structure representing a value (e.g. an int). 
 */
typedef struct aml_value {
	aml_value_type_t type;

	union {
		struct {
			uint64_t value;
		} intval;
	};
} aml_value_t;

/**
 * @brief A node inside the AML device tree.
 */
typedef struct aml_node {
	char name[AML_NAMESZ + 1];
	tree_node_t node;

	aml_op_t op;
	uint8_t *start;
	uint8_t *end;
} aml_node_t;

/**
 * @brief An scope when parsing AML code.
 */
typedef struct aml_scope {
	struct aml_scope *parent;
	aml_node_t *node;
	uint8_t *ptr;

#if notyet
	aml_value_t *locals;
	aml_value_t *args;
	aml_value_t *ret;
#endif
} aml_scope_t;

/** 
 * @brief The root aml node.
 */
extern aml_node_t aml_root;

/**
 * @brief Return the end AML bytecode pointer of a scope.
 */
static inline uint8_t *aml_scope_end(aml_scope_t *scope) {
	return scope->node->end;
}

/**
 * @brief Parse the AML bytecode found in the DSDT.
 */
void aml_parse_dsdt(uint8_t *ptr, size_t length);

#if 0
typedef enum aml_value_type {
	AML_VAL_NONE,
	AML_VAL_INT,
	AML_VAL_STR,
	AML_VAL_PKG,
	AML_VAL_BUF,
	AML_VAL_OBJREF,
	AML_VAL_PROC,
	AML_VAL_OPREGION,
	AML_VAL_MTHD,
	
	AML_VAL_FIELD,
	AML_VAL_INDEXFIELD,
	AML_VAL_BUFFIELD,

	AML_VAL_SCOPE,
	AML_VAL_DEV,
	AML_VAL_MTX,

	AML_VAL_THERM,
} aml_value_type_t;

typedef struct aml_value {
	aml_value_type_t type;
	size_t length;
	size_t ref;
	struct aml_node *node;
} aml_value_t;

typedef struct aml_node {
	tree_node_t node;
	aml_value_t *value;
} aml_node_t;

/*
 * TODO rename to aml_scope
 */
typedef struct aml_prog {
	uint8_t *start;
	uint8_t *end;
	uint8_t *cur;
	aml_node_t *node;
	struct aml_prog *parent;
	aml_value_t *locals;
	aml_value_t *args;
	aml_value_t *ret;
	int depth;
	uint16_t type;
	uint16_t cur_op;
} aml_prog_t;

aml_prog_t *aml_prog_alloc();

aml_value_t *aml_value_alloc();
aml_value_t *aml_value_ref();
#endif

#endif