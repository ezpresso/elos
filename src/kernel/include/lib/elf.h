/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef LIB_ELF_H
#define LIB_ELF_H

/* Symbolic values for the entries in the auxiliary table
   put on the initial stack */
#define AT_NULL   0	/* end of vector */
#define AT_IGNORE 1	/* entry should be ignored */
#define AT_EXECFD 2	/* file descriptor of program */
#define AT_PHDR   3	/* program headers for program */
#define AT_PHENT  4	/* size of program header entry */
#define AT_PHNUM  5	/* number of program headers */
#define AT_PAGESZ 6	/* system page size */
#define AT_BASE   7	/* base address of interpreter */
#define AT_FLAGS  8	/* flags */
#define AT_ENTRY  9	/* entry point of program */
#define AT_NOTELF 10	/* program is not ELF */
#define AT_UID    11	/* real uid */
#define AT_EUID   12	/* effective uid */
#define AT_GID    13	/* real gid */
#define AT_EGID   14	/* effective gid */
#define AT_PLATFORM 15  /* string identifying CPU for optimizations */
#define AT_HWCAP  16    /* arch dependent hints at CPU capabilities */
#define AT_CLKTCK 17	/* frequency at which times() increments */
/* AT_* values 18 through 22 are reserved */
#define AT_SECURE 23   /* secure mode boolean */
#define AT_BASE_PLATFORM 24	/* string identifying real platform, may
				 * differ from AT_PLATFORM. */
#define AT_RANDOM 25	/* address of 16 random bytes */
#define AT_RANDOM_NUM	16
#define AT_HWCAP2 26	/* extension of AT_HWCAP */
#define AT_EXECFN  31	/* filename of program */

/* 32-bit ELF base types. */
typedef uint32_t	elf32_addr_t;
typedef uint16_t	elf32_half_t;
typedef uint32_t	elf32_off_t;
typedef int32_t		elf32_sword_t;
typedef uint32_t	elf32_word_t;

/* 64-bit ELF base types. */
typedef uint64_t	elf64_addr_t;
typedef uint16_t	elf64_half_t;
typedef int16_t		elf64_shalf	;
typedef uint64_t	elf64_off_t;
typedef int32_t		elf64_sword_t;
typedef uint32_t	elf64_word_t;
typedef uint64_t	elf64_xword_t;
typedef int64_t		elf64_sxword_t;

/* These constants define the various ELF target machines */
#define EM_NONE		0
#define EM_M32		1
#define EM_SPARC	2
#define EM_386		3
#define EM_68K		4
#define EM_88K		5
#define EM_486		6	/* Perhaps disused */
#define EM_860		7
#define EM_MIPS		8	/* MIPS R3000 (officially, big-endian only) */
				/* Next two are historical and binaries and
				   modules of these types will be rejected by
				   Linux.  */
#define EM_MIPS_RS3_LE	10	/* MIPS R3000 little-endian */
#define EM_MIPS_RS4_BE	10	/* MIPS R4000 big-endian */

#define EM_PARISC	15	/* HPPA */
#define EM_SPARC32PLUS	18	/* Sun's "v8plus" */
#define EM_PPC		20	/* PowerPC */
#define EM_PPC64	21	 /* PowerPC64 */
#define EM_SPU		23	/* Cell BE SPU */
#define EM_ARM		40	/* ARM 32 bit */
#define EM_SH		42	/* SuperH */
#define EM_SPARCV9	43	/* SPARC v9 64-bit */
#define EM_H8_300	46	/* Renesas H8/300 */
#define EM_IA_64	50	/* HP/Intel IA-64 */
#define EM_X86_64	62	/* AMD x86-64 */
#define EM_S390		22	/* IBM S/390 */
#define EM_CRIS		76	/* Axis Communications 32-bit embedded processor */
#define EM_M32R		88	/* Renesas M32R */
#define EM_MN10300	89	/* Panasonic/MEI MN10300, AM33 */
#define EM_OPENRISC     92     /* OpenRISC 32-bit embedded processor */
#define EM_BLACKFIN     106     /* ADI Blackfin Processor */
#define EM_ALTERA_NIOS2	113	/* Altera Nios II soft-core processor */
#define EM_TI_C6000	140	/* TI C6X DSPs */
#define EM_AARCH64	183	/* ARM 64 bit */
#define EM_TILEPRO	188	/* Tilera TILEPro */
#define EM_MICROBLAZE	189	/* Xilinx MicroBlaze */
#define EM_TILEGX	191	/* Tilera TILE-Gx */
#define EM_BPF		247	/* Linux BPF - in-kernel virtual machine */
#define EM_FRV		0x5441	/* Fujitsu FR-V */

/*
 * This is an interim value that we will use until the committee comes
 * up with a final number.
 */
#define EM_ALPHA	0x9026

/* Bogus old m32r magic number, used by old tools. */
#define EM_CYGNUS_M32R	0x9041
/* This is the old interim value for S/390 architecture */
#define EM_S390_OLD	0xA390
/* Also Panasonic/MEI MN10300, AM33 */
#define EM_CYGNUS_MN10300 0xbeef

/* These constants are for the segment types stored in the image headers */
#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_SHLIB   5
#define PT_PHDR    6
#define PT_TLS     7               /* Thread local storage segment */
#define PT_LOOS    0x60000000      /* OS-specific */
#define PT_HIOS    0x6fffffff      /* OS-specific */
#define PT_LOPROC  0x70000000
#define PT_HIPROC  0x7fffffff
#define PT_GNU_EH_FRAME	0x6474e550
#define PT_GNU_STACK	(PT_LOOS + 0x474e551)
#define PT_GNU_RELRO 	0x6474e552

/*
 * Extended Numbering
 *
 * If the real number of program header table entries is larger than
 * or equal to PN_XNUM(0xffff), it is set to sh_info field of the
 * section header at index 0, and PN_XNUM is set to e_phnum
 * field. Otherwise, the section header at index 0 is zero
 * initialized, if it exists.
 *
 * Specifications are available in:
 *
 * - Oracle: Linker and Libraries.
 *   Part No: 817–1984–19, August 2011.
 *   http://docs.oracle.com/cd/E18752_01/pdf/817-1984.pdf
 *
 * - System V ABI AMD64 Architecture Processor Supplement
 *   Draft Version 0.99.4,
 *   January 13, 2010.
 *   http://www.cs.washington.edu/education/courses/cse351/12wi/supp-docs/abi.pdf
 */
#define PN_XNUM 0xffff

/* These constants define the different elf file types */
#define ET_NONE   0
#define ET_REL    1
#define ET_EXEC   2
#define ET_DYN    3
#define ET_CORE   4
#define ET_LOPROC 0xff00
#define ET_HIPROC 0xffff

/* This is the info that is needed to parse the dynamic section of the file */
#define DT_NULL		0
#define DT_NEEDED	1
#define DT_PLTRELSZ	2
#define DT_PLTGOT	3
#define DT_HASH		4
#define DT_STRTAB	5
#define DT_SYMTAB	6
#define DT_RELA		7
#define DT_RELASZ	8
#define DT_RELAENT	9
#define DT_STRSZ	10
#define DT_SYMENT	11
#define DT_INIT		12
#define DT_FINI		13
#define DT_SONAME	14
#define DT_RPATH 	15
#define DT_SYMBOLIC	16
#define DT_REL	        17
#define DT_RELSZ	18
#define DT_RELENT	19
#define DT_PLTREL	20
#define DT_DEBUG	21
#define DT_TEXTREL	22
#define DT_JMPREL	23
#define DT_ENCODING	32
#define OLD_DT_LOOS	0x60000000
#define DT_LOOS		0x6000000d
#define DT_HIOS		0x6ffff000
#define DT_VALRNGLO	0x6ffffd00
#define DT_VALRNGHI	0x6ffffdff
#define DT_ADDRRNGLO	0x6ffffe00
#define DT_ADDRRNGHI	0x6ffffeff
#define DT_VERSYM	0x6ffffff0
#define DT_RELACOUNT	0x6ffffff9
#define DT_RELCOUNT	0x6ffffffa
#define DT_FLAGS_1	0x6ffffffb
#define DT_VERDEF	0x6ffffffc
#define	DT_VERDEFNUM	0x6ffffffd
#define DT_VERNEED	0x6ffffffe
#define	DT_VERNEEDNUM	0x6fffffff
#define OLD_DT_HIOS     0x6fffffff
#define DT_LOPROC	0x70000000
#define DT_HIPROC	0x7fffffff

/* This info is needed when parsing the symbol table */
#define STB_LOCAL  0
#define STB_GLOBAL 1
#define STB_WEAK   2

#define STT_NOTYPE  0
#define STT_OBJECT  1
#define STT_FUNC    2
#define STT_SECTION 3
#define STT_FILE    4
#define STT_COMMON  5
#define STT_TLS     6

#define ELF_ST_BIND(x)		((x) >> 4)
#define ELF_ST_TYPE(x)		(((unsigned int) x) & 0xf)
#define ELF32_ST_BIND(x)	ELF_ST_BIND(x)
#define ELF32_ST_TYPE(x)	ELF_ST_TYPE(x)
#define ELF64_ST_BIND(x)	ELF_ST_BIND(x)
#define ELF64_ST_TYPE(x)	ELF_ST_TYPE(x)

typedef struct dynamic {
	elf32_sword_t d_tag;
	union{
		elf32_sword_t	d_val;
		elf32_addr_t	d_ptr;
	} d_un;
} elf32_dyn_t;

typedef struct {
	elf64_sxword_t d_tag;		/* entry tag value */
	union {
		elf64_xword_t d_val;
		elf64_addr_t d_ptr;
	} d_un;
} elf64_dyn_t;

/* The following are used with relocations */
#define ELF32_R_SYM(x) ((x) >> 8)
#define ELF32_R_TYPE(x) ((x) & 0xff)

#define ELF64_R_SYM(i)	((i) >> 32)
#define ELF64_R_TYPE(i)	((i) & 0xffffffff)

typedef struct elf32_rel {
	elf32_addr_t	r_offset;
	elf32_word_t	r_info;
} elf32_rel_t;

typedef struct elf64_rel {
	elf64_addr_t r_offset;	/* Location at which to apply the action */
	elf64_xword_t r_info;	/* index and type of relocation */
} elf64_rel_t;

typedef struct elf32_rela {
	elf32_addr_t	r_offset;
	elf32_word_t	r_info;
	elf32_sword_t	r_addend;
} elf32_rela_t;

typedef struct elf64_rela {
	elf64_addr_t r_offset;	/* Location at which to apply the action */
	elf64_xword_t r_info;	/* index and type of relocation */
	elf64_sxword_t r_addend; /* Constant addend used to compute value */
} elf64_rela_t;

typedef struct elf32_sym {
	elf32_word_t	st_name;
	elf32_addr_t	st_value;
	elf32_word_t	st_size;
	unsigned char	st_info;
	unsigned char	st_other;
	elf32_half_t	st_shndx;
} elf32_sym_t;

typedef struct elf64_sym {
	elf64_word_t st_name;		/* Symbol name, index in string tbl */
	unsigned char	st_info;	/* Type and binding attributes */
	unsigned char	st_other;	/* No defined meaning, 0 */
	elf64_half_t st_shndx;		/* Associated section index */
	elf64_addr_t st_value;		/* Value of the symbol */
	elf64_xword_t st_size;		/* Associated symbol size */
} elf64_sym_t;

#define EI_NIDENT	16

typedef struct elf32_hdr {
	unsigned char	e_ident[EI_NIDENT];
	elf32_half_t	e_type;
	elf32_half_t	e_machine;
	elf32_word_t	e_version;
	elf32_addr_t	e_entry;  /* Entry point */
	elf32_off_t		e_phoff;
	elf32_off_t		e_shoff;
	elf32_word_t	e_flags;
	elf32_half_t	e_ehsize;
	elf32_half_t	e_phentsize;
	elf32_half_t	e_phnum;
	elf32_half_t	e_shentsize;
	elf32_half_t	e_shnum;
	elf32_half_t	e_shstrndx;
} elf32_ehdr_t;

typedef struct elf64_hdr {
	unsigned char	e_ident[EI_NIDENT]; /* ELF "magic number" */
	elf64_half_t e_type;
	elf64_half_t e_machine;
	elf64_word_t e_version;
	elf64_addr_t e_entry; /* Entry point virtual address */
	elf64_off_t  e_phoff; /* Program header table file offset */
	elf64_off_t  e_shoff; /* Section header table file offset */
	elf64_word_t e_flags;
	elf64_half_t e_ehsize;
	elf64_half_t e_phentsize;
	elf64_half_t e_phnum;
	elf64_half_t e_shentsize;
	elf64_half_t e_shnum;
	elf64_half_t e_shstrndx;
} elf64_ehdr_t;

/*
 * These constants define the permissions on sections in the program
 * header, p_flags.
 */
#define PF_R	0x4
#define PF_W	0x2
#define PF_X	0x1

typedef struct elf32_phdr {
	elf32_word_t	p_type;
	elf32_off_t	p_offset;
	elf32_addr_t	p_vaddr;
	elf32_addr_t	p_paddr;
	elf32_word_t	p_filesz;
	elf32_word_t	p_memsz;
	elf32_word_t	p_flags;
	elf32_word_t	p_align;
} elf32_phdr_t;

typedef struct elf64_phdr {
	elf64_word_t p_type;
	elf64_word_t p_flags;
	elf64_off_t  p_offset;	/* Segment file offset */
	elf64_addr_t p_vaddr;	/* Segment virtual address */
	elf64_addr_t p_paddr;	/* Segment physical address */
	elf64_xword_t p_filesz;	/* Segment size in file */
	elf64_xword_t p_memsz;	/* Segment size in memory */
	elf64_xword_t p_align;	/* Segment alignment, file & memory */
} elf64_phdr_t;

/* sh_type */
#define SHT_NULL	0
#define SHT_PROGBITS	1
#define SHT_SYMTAB	2
#define SHT_STRTAB	3
#define SHT_RELA	4
#define SHT_HASH	5
#define SHT_DYNAMIC	6
#define SHT_NOTE	7
#define SHT_NOBITS	8
#define SHT_REL		9
#define SHT_SHLIB	10
#define SHT_DYNSYM	11
#define SHT_NUM		12
#define SHT_LOPROC	0x70000000
#define SHT_HIPROC	0x7fffffff
#define SHT_LOUSER	0x80000000
#define SHT_HIUSER	0xffffffff

/* sh_flags */
#define SHF_WRITE		0x1
#define SHF_ALLOC		0x2
#define SHF_EXECINSTR		0x4
#define SHF_RELA_LIVEPATCH	0x00100000
#define SHF_RO_AFTER_INIT	0x00200000
#define SHF_MASKPROC		0xf0000000

/* special section indexes */
#define SHN_UNDEF	0
#define SHN_LORESERVE	0xff00
#define SHN_LOPROC	0xff00
#define SHN_HIPROC	0xff1f
#define SHN_LIVEPATCH	0xff20
#define SHN_ABS		0xfff1
#define SHN_COMMON	0xfff2
#define SHN_HIRESERVE	0xffff
 
typedef struct elf32_shdr {
	elf32_word_t	sh_name;
	elf32_word_t	sh_type;
	elf32_word_t	sh_flags;
	elf32_addr_t	sh_addr;
	elf32_off_t	sh_offset;
	elf32_word_t	sh_size;
	elf32_word_t	sh_link;
	elf32_word_t	sh_info;
	elf32_word_t	sh_addralign;
	elf32_word_t	sh_entsize;
} elf32_Shdr;

typedef struct elf64_shdr {
	elf64_word_t sh_name;		/* Section name, index in string tbl */
	elf64_word_t sh_type;		/* Type of section */
	elf64_xword_t sh_flags;		/* Miscellaneous section attributes */
	elf64_addr_t sh_addr;		/* Section virtual addr at execution */
	elf64_off_t sh_offset;		/* Section file offset */
	elf64_xword_t sh_size;		/* Size of section in bytes */
	elf64_word_t sh_link;		/* Index of another section */
	elf64_word_t sh_info;		/* Additional section information */
	elf64_xword_t sh_addralign;	/* Section alignment */
	elf64_xword_t sh_entsize;	/* Entry size if section holds table */
} elf64_Shdr;

#define	EI_MAG0		0		/* e_ident[] indexes */
#define	EI_MAG1		1
#define	EI_MAG2		2
#define	EI_MAG3		3
#define	EI_CLASS	4
#define	EI_DATA		5
#define	EI_VERSION	6
#define	EI_OSABI	7
#define	EI_PAD		8

#define	ELFMAG0		0x7f		/* EI_MAG */
#define	ELFMAG1		'E'
#define	ELFMAG2		'L'
#define	ELFMAG3		'F'
#define	ELFMAG		"\177ELF"
#define	SELFMAG		4

#define	ELFCLASSNONE	0		/* EI_CLASS */
#define	ELFCLASS32	1
#define	ELFCLASS64	2
#define	ELFCLASSNUM	3

#define ELFDATANONE	0		/* e_ident[EI_DATA] */
#define ELFDATA2LSB	1
#define ELFDATA2MSB	2

#define EV_NONE		0		/* e_version, EI_VERSION */
#define EV_CURRENT	1
#define EV_NUM		2

#define ELFOSABI_NONE	0
#define ELFOSABI_LINUX	3

#ifndef ELF_OSABI
#define ELF_OSABI ELFOSABI_NONE
#endif

/*
 * Notes used in ET_CORE. Architectures export some of the arch register sets
 * using the corresponding note types via the PTRACE_GETREGSET and
 * PTRACE_SETREGSET requests.
 */
#define NT_PRSTATUS	1
#define NT_PRFPREG	2
#define NT_PRPSINFO	3
#define NT_TASKSTRUCT	4
#define NT_AUXV		6
/*
 * Note to userspace developers: size of NT_SIGINFO note may increase
 * in the future to accomodate more fields, don't assume it is fixed!
 */
#define NT_SIGINFO      0x53494749
#define NT_FILE         0x46494c45
#define NT_PRXFPREG     0x46e62b7f /* copied from gdb5.1/include/elf/common.h */
#define NT_PPC_VMX	0x100		/* PowerPC Altivec/VMX registers */
#define NT_PPC_SPE	0x101		/* PowerPC SPE/EVR registers */
#define NT_PPC_VSX	0x102		/* PowerPC VSX registers */
#define NT_PPC_TAR	0x103		/* Target Address Register */
#define NT_PPC_PPR	0x104		/* Program Priority Register */
#define NT_PPC_DSCR	0x105		/* Data Stream Control Register */
#define NT_PPC_EBB	0x106		/* Event Based Branch Registers */
#define NT_PPC_PMU	0x107		/* Performance Monitor Registers */
#define NT_PPC_TM_CGPR	0x108		/* TM checkpointed GPR Registers */
#define NT_PPC_TM_CFPR	0x109		/* TM checkpointed FPR Registers */
#define NT_PPC_TM_CVMX	0x10a		/* TM checkpointed VMX Registers */
#define NT_PPC_TM_CVSX	0x10b		/* TM checkpointed VSX Registers */
#define NT_PPC_TM_SPR	0x10c		/* TM Special Purpose Registers */
#define NT_PPC_TM_CTAR	0x10d		/* TM checkpointed Target Address Register */
#define NT_PPC_TM_CPPR	0x10e		/* TM checkpointed Program Priority Register */
#define NT_PPC_TM_CDSCR	0x10f		/* TM checkpointed Data Stream Control Register */
#define NT_PPC_PKEY	0x110		/* Memory Protection Keys registers */
#define NT_386_TLS	0x200		/* i386 TLS slots (struct user_desc) */
#define NT_386_IOPERM	0x201		/* x86 io permission bitmap (1=deny) */
#define NT_X86_XSTATE	0x202		/* x86 extended state using xsave */
#define NT_S390_HIGH_GPRS	0x300	/* s390 upper register halves */
#define NT_S390_TIMER	0x301		/* s390 timer register */
#define NT_S390_TODCMP	0x302		/* s390 TOD clock comparator register */
#define NT_S390_TODPREG	0x303		/* s390 TOD programmable register */
#define NT_S390_CTRS	0x304		/* s390 control registers */
#define NT_S390_PREFIX	0x305		/* s390 prefix register */
#define NT_S390_LAST_BREAK	0x306	/* s390 breaking event address */
#define NT_S390_SYSTEM_CALL	0x307	/* s390 system call restart data */
#define NT_S390_TDB	0x308		/* s390 transaction diagnostic block */
#define NT_S390_VXRS_LOW	0x309	/* s390 vector registers 0-15 upper half */
#define NT_S390_VXRS_HIGH	0x30a	/* s390 vector registers 16-31 */
#define NT_S390_GS_CB	0x30b		/* s390 guarded storage registers */
#define NT_S390_GS_BC	0x30c		/* s390 guarded storage broadcast control block */
#define NT_S390_RI_CB	0x30d		/* s390 runtime instrumentation */
#define NT_ARM_VFP	0x400		/* ARM VFP/NEON registers */
#define NT_ARM_TLS	0x401		/* ARM TLS register */
#define NT_ARM_HW_BREAK	0x402		/* ARM hardware breakpoint registers */
#define NT_ARM_HW_WATCH	0x403		/* ARM hardware watchpoint registers */
#define NT_ARM_SYSTEM_CALL	0x404	/* ARM system call number */
#define NT_ARM_SVE	0x405		/* ARM Scalable Vector Extension registers */
#define NT_METAG_CBUF	0x500		/* Metag catch buffer registers */
#define NT_METAG_RPIPE	0x501		/* Metag read pipeline state */
#define NT_METAG_TLS	0x502		/* Metag TLS pointer */
#define NT_ARC_V2	0x600		/* ARCv2 accumulator/extra registers */

/* Note header in a PT_NOTE section */
typedef struct elf32_note {
	elf32_word_t n_namesz;	/* Name size */
	elf32_word_t n_descsz;	/* Content size */
	elf32_word_t n_type;		/* Content type */
} elf32_Nhdr;

/* Note header in a PT_NOTE section */
typedef struct elf64_note {
	elf64_word_t n_namesz;	/* Name size */
	elf64_word_t n_descsz;	/* Content size */
	elf64_word_t n_type;	/* Content type */
} elf64_Nhdr;

#endif /* _UAPI_LINUX_ELF_H */