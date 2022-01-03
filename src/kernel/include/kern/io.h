#ifndef KERN_IO_H
#define KERN_IO_H

#define readb(addr) (*(volatile uint8_t *) (addr))
#define readw(addr) (*(volatile uint16_t *) (addr))
#define readl(addr) (*(volatile uint32_t *) (addr))
#define readq(addr) (*(volatile uint64_t *) (addr))
#define writeb(addr,b) ((*(volatile uint8_t *) (addr)) = (b))
#define writew(addr,b) ((*(volatile uint16_t *) (addr)) = (b))
#define writel(addr,b) ((*(volatile uint32_t *) (addr)) = (b))
#define writeq(addr,b) ((*(volatile uint64_t *) (addr)) = (b))

#endif
