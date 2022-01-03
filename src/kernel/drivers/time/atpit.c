/*
 * ███████╗██╗      ██████╗ ███████╗
 * ██╔════╝██║     ██╔═══██╗██╔════╝
 * █████╗  ██║     ██║   ██║███████╗
 * ██╔══╝  ██║     ██║   ██║╚════██║
 * ███████╗███████╗╚██████╔╝███████║
 * ╚══════╝╚══════╝ ╚═════╝ ╚══════╝
 * 
 * Copyright (c) 2017, Elias Zell
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <kern/system.h>
#include <kern/time.h>
#include <device/device.h>
#include <device/evtimer.h>
#include <device/timecounter.h>
#include <drivers/bus/isa.h>

typedef struct atpit {
	bus_res_t io_res;
	bus_res_t irq_res;

	/*
	 * TODO It would be better if the mainboard driver could
	 * choose which timers can be used for event subsys
	 * (on PC the other 2 counters of atpit are usually used for
	 * isa-dma and device speaker).
	 */
	evtimer_t ev;
	timecounter_t tc;

	/*
	 * The lock is needed to synchronize access of the fields below
	 * between atpit_intr and atpit_counter. Furthermore it synchronizes
	 * parallel calls to atpit_counter.
	 */
	sync_t lock;
	uint8_t ticked;
	uint16_t last;
	uint64_t offset;
	uint32_t cntrmax;
} atpit_t;

#define FREQ	1193182
#define CNTR0	0x0
#define CNTR1	0x1
#define CNTR2	0x2
#define MODE	0x3
#define		INTTC	0x02	/* Mode 0: interrupt on terminal count */
#define 	RATEGEN	0x04	/* Mode 2 */
#define		LO_HI	(3 << 4) /* lobyte/hibyte access mode */
#define 	SEL0	(0 << 6)
#define 	SEL1	(1 << 6)
#define		SEL2	(2 << 6)
#define 	LATCH	(0)
#define MAXCNTR 0x10000
#define MINPERIOD ((0x0002ULL * SEC_NANOSECS) / FREQ)

static int atpit_intr(void *arg, __unused int intr) {
	atpit_t *pit = arg;

	synchronized(&pit->lock) {
		/*
		 * A concurrent call to atpit_counter might have
		 * already incremented pit->offset.
		 */
		if(pit->ticked == 0) {
			pit->offset += pit->cntrmax;
			pit->last = 0;
		} else {
			pit->ticked = 0;
		}
	}

	evtimer_intr(&pit->ev);
	return BUS_INTR_OK;
}

static void atpit_ev_config(evtimer_t *timer, evtimer_mode_t mode,
	uint64_t cntr)
{
	atpit_t *pit = evtimer_priv(timer);

	if(mode != EV_PERIODIC) {
		kpanic("TODO");
	}

	/*
	 * TODO we might be loosing some ticks when configuring the timer
	 * while no interrupt occured.
	 */

	pit->cntrmax = cntr;
	if(cntr == MAXCNTR) {
		cntr = 0;
	}

#if 1
	/*
	 * This is not necesserily very accurate, but it suffices
	 * for now. The alternative would be read the counter again.
	 */
	pit->offset += pit->last;
#else
	if(pit->cntrmax != (uint32_t)-1) {
		uint16_t cntr;
		bus_res_writeb(&pit->io_res, MODE, SEL0 | LATCH);
		cntr = bus_res_readb(&pit->io_res, CNTR0);
		cntr |= bus_res_readb(&pit->io_res, CNTR0) << 8;
		/*
		 * TODO any wrap around issues?
		 */
		pit->offset += pit->cntrmax - cntr;
	}
#endif

	pit->last = 0;
	pit->ticked = 0;

	bus_res_writeb(&pit->io_res, MODE, RATEGEN | LO_HI | SEL0);
	bus_res_writeb(&pit->io_res, CNTR0, cntr & 0xFF);
	bus_res_writeb(&pit->io_res, CNTR0, (cntr >> 8) & 0xff);
}

static void atpit_ev_stop(evtimer_t *timer) {
	atpit_ev_config(timer, EV_PERIODIC, MAXCNTR);
}

static uint64_t atpit_counter(timecounter_t *tc) {
	atpit_t *pit = tc_priv(tc);
	uint16_t cntr;

	sync_scope_acquire(&pit->lock);
	bus_res_writeb(&pit->io_res, MODE, SEL0 | LATCH);
	cntr = bus_res_readb(&pit->io_res, CNTR0);
	cntr |= bus_res_readb(&pit->io_res, CNTR0) << 8;

	assert(cntr <= pit->cntrmax);
	cntr = pit->cntrmax - cntr;
	if(cntr < pit->last) {
		pit->ticked = 1;
		pit->offset += pit->cntrmax;
	}

	pit->last = cntr;
	return pit->offset + cntr;
}

static int atpit_probe(__unused device_t *device) {
	return DEVICE_PROBE_OK;
}

static int atpit_attach(device_t *device) {
	atpit_t *pit = device_priv(device);
	int err;

	err = bus_alloc_res_name(device, BUS_RES_IO, 0, "io", RF_MAP,
		&pit->io_res);
	if(err) {
		return err;
	}

	err = bus_alloc_res_name(device, BUS_RES_INTR, 0, "intr", RF_NONE,
		&pit->irq_res);
	if(err) {
		goto err_intr;
	}

	/*
	 * The pit has a 16bit counter, but we claim that is has a 64bit
	 * counter.
	 */
	sync_init(&pit->lock, SYNC_SPINLOCK);
	pit->offset = 0;
	pit->cntrmax = (uint32_t)-1;
	pit->last = 0;
	pit->ticked = 0;

	pit->ev.name = "atpit";
	pit->ev.priv = pit;
	pit->ev.flags = EV_F_PERIODIC | EV_F_ONESHOT;
	pit->ev.cpu = CPU_ID_ANY;
	pit->ev.max_period = (0xfffeULL * SEC_NANOSECS) / FREQ;
	pit->ev.min_period = MINPERIOD;
	pit->ev.freq = FREQ;
	pit->ev.config = atpit_ev_config;
	pit->ev.stop = atpit_ev_stop;
	atpit_ev_stop(&pit->ev);

	/*
	 * The pit can be used for events and for measuring time.
	 */
	pit->tc.name = "atpit";
	pit->tc.priv = pit;
	pit->tc.freq = FREQ;
	pit->tc.mask = (uint64_t)-1;
	pit->tc.quality = 0;
	pit->tc.read = atpit_counter;

	evtimer_register(&pit->ev);
	tc_register(&pit->tc);

	err = bus_setup_intr(device, atpit_intr, NULL, pit, &pit->irq_res);
	if(err) {
		goto err_map;
	}

	device_set_desc(device, "AT Programmable Interval Timer");

 	return 0;

err_map:
	evtimer_unregister(&pit->ev);
	bus_free_res(device, &pit->irq_res);
err_intr:
	bus_free_res(device, &pit->io_res);
	return err;
}

static int atpit_detach(device_t *device) {
	atpit_t *pit = device_priv(device);
	int err;

	err = tc_unregister(&pit->tc);
	if(err) {
		return err;
	}

	err = evtimer_unregister(&pit->ev);
	if(err) {
		return err;
	}

	bus_free_res(device, &pit->irq_res);
	bus_free_res(device, &pit->io_res);

	return 0;
}

static DEVTAB(atpit_devtab, "atpit", "isa") {
	{ .id = ISA_ID(ISA_VEN_PNP, 0x0100) /* PNP0100 */ },
	{ DEVTAB_END }
};

static DRIVER(atpit_drivers) {
	.name = "atpit",
	.flags = DEV_DEP_INTR,
	.type = DEVICE_TIMER,
	.private = sizeof(atpit_t),
	.devtab = &atpit_devtab,
	.probe = atpit_probe,
	.attach = atpit_attach,
	.detach = atpit_detach,
};
