#ifndef ACPI_AMLPRIV_H
#define ACPI_AMLPRIV_H

#define AML_OP_SCOPE		0x10
#define AML_OP_EXTPREFIX	0x5B
#define AML_OP_LNOT		0x92

/*
 * AML opcode argument types:
 */
#define AML_ARG_PKGLEN		'p' /* PkgLength */
#define AML_ARG_NAME_CREATE	'n' /* NameString (create node)*/
#define AML_ARG_NAME		'N' /* NameString (lookup node) */
#define AML_ARG_TERMLIST	'T' /* TermList */

static inline uint8_t aml_next(aml_scope_t *scope) {
	if(scope->ptr >= aml_scope_end(scope)) {
		kpanic("[aml] scope: expected more bytes");
	}

	return *scope->ptr++;
}

static inline uint8_t aml_peek(aml_scope_t *scope) {
	return *scope->ptr;
}

#endif