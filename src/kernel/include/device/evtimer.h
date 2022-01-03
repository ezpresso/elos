#ifndef DEVICE_EVTIMER_H
#define DEVICE_EVTIMER_H

#include <lib/list.h>

typedef void (*ev_callback_t) (void *arg);

typedef enum evtimer_mode {
	EV_PERIODIC = 0,
	EV_ONESHOT,
} evtimer_mode_t;

typedef struct evtimer {
	void *arg;
	ev_callback_t callback;
	list_node_t node;

	/*
	 * Provided by the driver.
	 */
	const char *name;
	nanosec_t min_period;
	nanosec_t max_period;
	frequency_t freq;
	void *priv;
	cpu_id_t cpu;

#define EV_F_PERIODIC	(1 << 0)
#define EV_F_ONESHOT	(1 << 1)
/* This flag is not set by the driver, the
 * driver only sets the cpu field. This flag
 * is only used for evtimer_get.
 */
#define EV_F_CPULOCAL	(1 << 2)
	int flags;

	void (*config)	(struct evtimer *, evtimer_mode_t mode, uint64_t cntr);
	void (*stop)	(struct evtimer *);
} evtimer_t;

static inline void *evtimer_priv(evtimer_t *timer) {
	return timer->priv;
}

/**
 * @brief Register an event timer.
 */
void evtimer_register(evtimer_t *timer);

/**
 * @brief Unregister an event timer.
 */
int evtimer_unregister(evtimer_t *timer);

/**
 * @brief Handle an interrupt of an event timer.
 */
void evtimer_intr(evtimer_t *timer);

evtimer_t *evtimer_get(int flags, ev_callback_t cb, void *arg);
void evtimer_put(evtimer_t *timer);
void evtimer_config(evtimer_t *timer, evtimer_mode_t mode, nanosec_t time);
void evtimer_stop(evtimer_t *timer);

#endif