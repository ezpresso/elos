#ifndef ARCH_BUS_H
#define ARCH_BUS_H

#define BUS_ADDR_MAX UINT32_MAX

typedef uint32_t bus_addr_t;

extern struct bus_res_acc x86_io_acc;
extern struct bus_res_acc x86_mem_acc;

int arch_pci_init(void);

#endif