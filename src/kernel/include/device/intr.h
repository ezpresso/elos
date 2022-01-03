#ifndef DEVICE_INTR_H
#define DEVICE_INTR_H

#include <kern/wait.h>
#include <lib/list.h>
#include <device/device.h>

/*
 * Some simple helpers for devices offering interrupts to
 * their interrupt children. If you're looking for a way
 * to allocate an interrupt for your device, you're looking
 * in the wrong place. See bus_alloc_res and bus_setup_intr
 * instead. 
 */

/**
 * @brief A type describing an interrupt number.
 */
typedef size_t intnum_t;

typedef struct intr_src {
	struct intr_cntlr *cntlr;
	list_t handlers;

	/*
	 * This lock is used to synchronize bus_teardown_res and
	 * interrupt handling. Since interrupt handlers may not block,
	 * this is a spinlock.
	 */
	sync_t lock;

	/*
	 * The interrupt thread locks this lock rather than the
	 * spin spinlock above, since this lock does not enter
	 * a critical section. 
	 */
	sync_t mtx;
	
	/*
	 * _num_ counts the number of resources that allocated
	 * (but not necessarily enabled) the interrupt.
	 */
	size_t num;
	intnum_t intr;

	/*
	 * - BUS_TRIG_LVL or BUS_TRIG_EDGE
	 * - BUS_POL_HI or BUS_POL_LO
	 * - BUS_INTR_SHARED
	 */
	int flags;

	uint8_t pending;
	waitqueue_t wq;
	struct thread *ithr;
	size_t nthr; /* number of threaded handlers */
} intr_src_t;

typedef struct intr_cntlr {
	intr_src_t *intrs;
	intnum_t nintr;
	size_t nthr; /* Number of active interrupt threads */
	bool (*config) (struct intr_cntlr *cntlr, intnum_t num, int flags);
} intr_cntlr_t;

/**
 * @brief Initialize a structure of an interrupt controller.
 *
 * Initialize the @p nintr interrupt source structures pointed to by @p intr
 * and initialize the intr_cntlr structure. The interrupt sources should NOT
 * be initialized or configured before. If the interrupt controller driver
 * want's to set the e.g. polarity of an individual source, it has to use
 * intr_config.
 */
void intr_cntlr_init(intr_cntlr_t *cntlr, intr_src_t *intrs, intnum_t nintr);

/**
 * @brief Destroy an interrupt controller.
 */
void intr_cntlr_destroy(intr_cntlr_t *cntlr);

/**
 * @brief Allocate an interrupt from the controller.
 *
 * Used in bus_alloc_res callbacks of interrupt providers.
 */
int intr_alloc(intr_cntlr_t *cntlr, bus_res_req_t *req);

/**
 * @see intr_alloc
 */
int intr_free(intr_cntlr_t *cntlr, bus_res_t *res);

/**
 * Called by interrupt controllers on initialization (e.g. if the
 * source cannot be configured and has a specified polarity/trigger)
 */
void intr_config(intr_cntlr_t *cntlr, intnum_t intr, int flags);

/**
 * @brief Do not allow the allocation of a specified interrupt.
 */
void intr_reserve(intr_cntlr_t *cntlr, intnum_t intr);

/**
 * @brief Add an interrupt handler from an interrupt source.
 *
 * Used in the bus_setup_res callbacks to setup an interrupt resource.
 */
void intr_add_handler(intr_cntlr_t *cntlr, bus_res_t *res);

/**
 * @brief Remove an interrupt handler from an interrupt source.
 *
 * After the function returns, the handler will not be called again.
 * It also makes sure that every handler is currently running, has
 * returned before exiting.
 */
 void intr_remove_handler(intr_cntlr_t *cntlr, bus_res_t *res);

/**
 * @brief Handle an interrupt.
 */
int intr_handle(intr_cntlr_t *cntlr, intnum_t intr);

static inline intr_src_t *intr_src(intr_cntlr_t *cntlr, intnum_t intr) {
	assert(intr < cntlr->nintr);
	return &cntlr->intrs[intr];
}

#endif