#ifndef ARCH_DELAY_H
#define ARCH_DELAY_H

#include <kern/time.h>

/*
 * I would like to remove this header...
 */

#define I8254_CNTR0	0x40
#define I8254_MODE	0x43
#define I8254_FREQ	1193182
#define		INTTC	0x02	/* Mode 0: interrupt on terminal count */
#define 	RATEGEN	0x04	/* Mode 2 */
#define		LO_HI	(3 << 4) /* lobyte/hibyte access mode */
#define 	SEL0	(0 << 6)
#define 	SEL1	(1 << 6)
#define		SEL2	(2 << 6)
#define 	LATCH	(0)

static inline void early_delay_setup(void) {
	outb(I8254_MODE, SEL0 | INTTC | LO_HI);
	outb(I8254_CNTR0, 0x0);
	outb(I8254_CNTR0, 0x0);
}

static inline uint16_t i8254_read(void) {
	uint16_t cntr;
	outb(I8254_MODE, SEL0 | LATCH);
	cntr = inb(I8254_CNTR0);
	cntr |= inb(I8254_CNTR0) << 8;
	return cntr;
}

static inline void early_delay(nanosec_t ns) {
	uint16_t prev, tick;
	uint64_t delta;
	int64_t left;

	prev = i8254_read();
	left = (ns * I8254_FREQ + (SEC_NANOSECS - 1)) / SEC_NANOSECS;

	while(left > 0) {
		tick = i8254_read();

		/*
		 * The i8254 counts from 0x10000 down to 0 and not
		 * the other way. That's why we do 'prev - tick' and
		 * not 'tick - prev'.
		 */
		delta = (prev - tick) & 0xFFFF;
		prev = tick;
		left -= delta;
	}
}

#endif