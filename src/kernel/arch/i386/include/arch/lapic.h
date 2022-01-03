#ifndef ARCH_LAPIC_H
#define ARCH_LAPIC_H

#define LAPIC_IPI_OTHERS	-1
#define LAPIC_IPI_BCAST		-2

void lapic_ipi(int vec, int dest);
int lapic_ipi_wait(size_t loops);
int lapic_start_ap(int id, uint16_t addr);
uint8_t lapic_id(void);
void lapic_eoi(void);
void lapic_init(void);

#endif