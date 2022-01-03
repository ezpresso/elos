#ifndef ACPI_TABLE_H
#define ACPI_TABLE_H

#define ACPI_RSDP_1_LEN 20

typedef struct acpi_rsdp {
	uint8_t sig[8];
	uint8_t cs; /* checksum */
	uint8_t oem[6];

#define ACPI_RSDP_1 0 /* ACPI version 1.0 RSDP */
#define ACPI_RSDP_2 1
	uint8_t rev;
	uint32_t rsdt;

    /* The fields below are only valid for version 2 */
	uint32_t length;
	uint64_t xsdt;
	uint8_t ext_cs;
	uint8_t rsvd[3];
} __packed acpi_rsdp_t;

typedef struct acpi_sdt_hdr {
	uint8_t sig[4];
	uint32_t length;
	uint8_t revision;
	uint8_t cs;
	char oem_id[6];
	uint8_t oem_tab_id[8];
	uint32_t oem_rev;
	uint32_t creator_id;
	uint32_t creator_rev;
} __packed acpi_sdt_hdr_t;

/**
 * @brief ACPI generic address structure.
 */
typedef struct acpi_gas {
	uint8_t space;
	uint8_t bitwidth;
	uint8_t bitoff;
	uint8_t access;
	uint64_t address;
} __packed acpi_gas_t;

typedef struct acpi_fadt {
	acpi_sdt_hdr_t hdr;
	uint32_t firmware_ctl;
	uint32_t dsdt;
} __packed acpi_fadt_t;

#endif
