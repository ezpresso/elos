#ifndef ARCH_ELF_H
#define ARCH_ELF_H

#define ELF_CLASS	ELFCLASS32
#define ELF_DATA	ELFDATA2LSB

#define AT_SYSINFO	32
#define AT_SYSINFO_EHDR	33

static inline bool elf_check_machine32(elf32_half_t machine) {
	return machine == EM_386 || machine == EM_486;
}

static inline bool elf_check_flags32(elf32_word_t flags) {
	return flags == 0;
}

#endif