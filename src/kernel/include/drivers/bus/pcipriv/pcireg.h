#ifndef DRIVERS_BUS_PCIPRIV_PCIREG_H
#define DRIVERS_BUS_PCIPRIV_PCIREG_H

/*
 * PCI config space
 */
#define PCI_CFG_VENDOR			0x00 /* Vendor ID */
#define PCI_CFG_DEVICE			0x02 /* Device ID */
#define PCI_CFG_CMD			0x04 /* Command register */
#define		PCI_CMD_IO			(1 << 0) /* Enable response in I/O space */
#define 	PCI_CMD_MEMORY			(1 << 1) /* Enable response in memory space */
#define		PCI_CMD_BUSMASTER		(1 << 2) /* Enable bus mastering */
#define 	PCI_CMD_SPECIAL			(1 << 3) /* Enable special cycles */
#define 	PCI_CMD_INVALIDATE		(1 << 4) /* Enable memory write and invalidate */
#define 	PCI_CMD_VGA_PALETTE		(1 << 5) /* Enable vga palette snooping */
#define 	PCI_CMD_PARITY			(1 << 6) /* Enable parity error response */
#define		PCI_CMD_STEPPING		(1 << 7) /* Enable address/data stepping */
#define		PCI_CMD_SERR			(1 << 8) /* Enable SERR */
#define		PCI_CMD_FAST_BACK  		(1 << 9) /* Enable back-to-back writes */
#define		PCI_CMD_INTX_DISABLE 		(1 << 10) /* INTx Emulation Disable */
#define PCI_CFG_STATUS			0x06
#define		PCI_STATUS_CAP_LIST		(1 << 4) /* Capabilities list*/
#define 	PCI_STATUS_66MHZ		(1 << 5) /* 66MHZ capable */
#define 	PCI_STATUS_FAST_BACK		(1 << 7) /* Fast back-to-back capable */
#define PCI_CFG_REVISION		0x08 /* Revision */
#define PCI_CFG_PROG_IF			0x09 /* Interface */
#define 	PCI_IDE_PRIM_MODE		(1 << 0) /* 0: compat, 1: native-PCI mode */
#define 	PCI_IDE_PRIM_BOTH		(1 << 1) /* 0: fixed, 1: both */
#define 	PCI_IDE_SEC_MODE		(1 << 2) /* 0: compat, 1: native-PCI mode */
#define 	PCI_IDE_SEC_BOTH		(1 << 3) /* 0: fixed, 1: both */
#define 	PCI_IDE_MASTER			(1 << 7)
#define PCI_CFG_SUBCLASS		0x0A /* Subclass code */
#define PCI_CFG_CLASS 			0x0B /* Class code */
#define PCI_CFG_CACHE_LINE		0x0C /* Cache line size */ 
#define PCI_CFG_LATENCY_TIMER		0x0D
#define PCI_CFG_HDR_TYPE		0x0E
#define 	PCI_HDR_TYPE_MASK		~(1 << 7)
#define 	PCI_HDR_TYPE_NORMAL		0x0
#define 	PCI_HDR_TYPE_PCI_BRIDGE		0x1
#define 	PCI_HDR_TYPE_CARDBUS		0x2
#define 	PCI_HDR_TYPE_MULT_FUNC		(1 << 7)
#define PCI_CFG_BIST			0x0F /* Built-in self test */
#define		PCI_BIST_RET_MASK		0x0f
#define 	PCI_BIST_START			(1 << 6)
#define 	PCI_BIST_CAP			(1 << 7)

/*
 * Configuration space for regular devices.
 */
#define PCI_DEVCFG_BAR_NUM		6
#define PCI_DEVCFG_BAR0			0x10
#define PCI_DEVCFG_BAR(x)		(PCI_DEVCFG_BAR0 + (x) * 0x04)
#define 	PCI_BAR_IO			(1 << 0)
#define 	PCI_BAR_TYPE_64			(2 << 1)
#define 	PCI_BAR_PREFETCH		(1 << 3)
#define 	PCI_BAR_ADDR_MEM(x)		((x) & (~0xFUL))
#define 	PCI_BAR_ADDR_IO(x)		((x) & (~0x3UL)) /* bit 2 is reserved */
#define PCI_DEVCFG_BAR1			0x14 /* Base address register 1 */
#define PCI_DEVCFG_BAR2			0x18 /* Base address register 2 */
#define PCI_DEVCFG_BAR3			0x1C /* Base address register 3 */
#define PCI_DEVCFG_BAR4			0x20 /* Base address register 4 */
#define PCI_DEVCFG_BAR5			0x24 /* Base address register 5 */
#define PCI_DEVCFG_CIS			0x28 /* Cardbus CIS Pointer */
#define PCI_DEVCFG_SUBSYS_VENDOR	0x2C /* Subsystem vendor ID */
#define PCI_DEVCFG_SUBSYS_ID		0x2E /* Subsystem device ID */
#define PCI_DEVCFG_XROM			0x30 /* PCI expansion rom */
#define 	PCI_XROM_ENABLE			(1 << 0)
#define 	PCI_XROM_ADDR(x)		((x) & 0x7FF)	
#define PCI_DEVCFG_CAP_PTR		0x34 /* Capability pointer */
#define PCI_DEVCFG_INT_LINE		0x3C /* Interrupt line (practically useless) */
#define PCI_DEVCFG_INT_PIN		0x3D /* Interrupt PIN */
#define 	PCI_PIN_NONE			0x0
#define 	PCI_PIN_INTA			0x1
#define 	PCI_PIN_INTB			0x2
#define 	PCI_PIN_INTC			0x3
#define 	PCI_PIN_INTD			0x4
#define PCI_DEVCFG_MINGRANT		0x3E
#define PCI_DEVCFG_MAX_LATENCY		0x3F

/*
 * Configuration space for PCI-to-PCI bridges.
 */
#define PCI_P2PCFG_BAR_NUM		2
#define PCI_P2PCFG_BAR0			PCI_DEVCFG_BAR0
#define PCI_P2PCFG_BAR1			PCI_DEVCFG_BAR1
#define PCI_P2PCFG_PRIM			0x18 /* Primary Bus Number */				
#define PCI_P2PCFG_SEC			0x19 /* Secondary Bus Number */
#define PCI_P2PCFG_SUB			0x1A /* Subordinate Bus Number */
#define PCI_P2PCFG_SEC_LT		0x1B /* Secondary Latency Timer */
#define PCI_P2PCFG_IO_BASE		0x1C /* IO window base */
#define PCI_P2PCFG_IO_LIMIT		0x1D /* IO window limit */
#define PCI_P2PCFG_SEC_STA		0x1E /* Secondary Status */
#define PCI_P2PCFG_MEM_BASE		0x20 /* Memory base */
#define PCI_P2PCFG_MEM_LIMIT		0x22 /* Memory limit */
#define PCI_P2PCFG_PMEM_BASE		0x24 /* Prefetchable memory base */
#define PCI_P2PCFG_PMEM_LIMIT		0x26 /* Prefetchable memory limit */
#define PCI_P2PCFG_PMEM_BASE_HI		0x28 /* Prefetchable base (upper 32 bits) */
#define PCI_P2PCFG_PMEM_LIMIT_HI	0x2c /* Prefetchable limit (upper 32 bits) */
#define PCI_P2PCFG_IO_BASE_HI		0x30 /* I/O base (upper 16 bits) */
#define PCI_P2PCFG_IO_LIMIT_HI		0x32 /* I/O limit (upper 16 bits) */
#define PCI_P2PCFG_CAP_PTR		0x34 /* Capability pointer */
#define PCI_P2PCFG_EXPROM		0x38 /* Expansion ROM address */
#define PCI_P2PCFG_INT_LINE 		0x3C /* Interrupt line (practically useless) */
#define PCI_P2PCFG_INT_PIN	 	0x3D /* Interrupt PIN */
#define PCI_P2PCFG_BCNTRL		0x3E
#define 	PCI_P2P_BCNTRL_ISA	 	(1 << 2) /* Respond in IO window (0x0000 - 0xFFFF) */
#define 	PCI_P2P_BCNTRL_VGA 		(1 << 3) /* Respond to legacy VGA registers  */

/*
 * PCI capability numbers.
 */
#define PCI_CAP_END		0x00
#define PCI_CAP_PM		0x01 /* PCI Power management interface */
#define PCI_CAP_AGP		0x02
#define PCI_CAP_VPD		0x03
#define 	PCI_VPD_ADDR		0x02 /* word */
#define 		PCI_VPD_ADDR_MASK	~(1 << 15)
#define 		PCI_VPD_FINISHED	(1 << 15)
#define 	PCI_VPD_DATA		0x04
#define PCI_CAP_SLOT_ID		0x04 /* Slot identification */
#define PCI_CAP_MSI		0x05
#define PCI_CAP_HOT_SWAP	0x06 /* CompatPCI Hot Swap */
#define PCI_CAP_PCIX		0x07
#define PCI_CAP_MSIX		0x11
#define PCI_CAP_SATA_IDP	0x12 /* Serial ATA Index-Data Pair access */

/**
 * @brief The header of a PCI ROM.
 */
typedef struct pci_rom_header {
#define PCI_ROM_SIG 0xAA55
	uint16_t sig; /* 0xAA55 le */
	uint8_t	 size;
	uint8_t	 init[3]; /* init opcode (jmp) */
	uint8_t	 rsvd[0x12];
	uint16_t data;
} __packed pci_rom_header_t;

/*
 * Remember: Last byte in rom is checksum
 */
typedef struct pci_rom_data {
#define PCI_DATA_SIG ((uint32_t) ('P' | ('C' << 8) | ('I' << 16) | ('R' << 24)))
	uint32_t sig;
	uint16_t vendor;
	uint16_t device;
	uint16_t rsvd;
	uint16_t pcir_len; /* PCI data structure length */
	uint8_t pcir_revision; /* PCI data structure revision */
	uint8_t class_lo;
	uint16_t class_hi;
	uint16_t img_len;
	uint16_t revision; /* code / data revision */
#define PCIR_CODE_TYPE_X86PC	0
#define PCIR_CODE_TYPE_OFW	1 /* Open Firmware standard for PCI */
#define PCIR_CODE_TYPE_PA_RISC	2 /* Hewlett-Packard PA-RISC */
	uint8_t code_type;
#define PCIR_INDICATOR_LAST	(1 << 7)
	uint8_t indicator;
	uint16_t rsvd2;
} __packed pci_rom_data_t;

#endif