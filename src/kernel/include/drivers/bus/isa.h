#ifndef DEVICE_BUS_ISA_H
#define DEVICE_BUS_ISA_H

#define ISA_VENDOR(v0, v1, v2)		\
	__builtin_bswap16(		\
	((((v0) - 0x40) & 0x1f)<<10) |	\
	((((v1) - 0x40) & 0x1f)<< 5) | 	\
	((((v2) - 0x40) & 0x1f)<< 0))

#define ISA_VEN_PNP		ISA_VENDOR('P','N','P')
#define ISA_ID(ven, dev) 	((ven) | (__builtin_bswap16(dev) << 16))
	
#endif