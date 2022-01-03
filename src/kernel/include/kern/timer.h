#ifndef KERN_TIMER_H
#define KERN_TIMER_H

#include <lib/list.h>

/**
 * The period in milliseconds of the periodick
 * system tick.
 */
#define TICK_PERIOD MILLI2NANO(5)

#define TIMER_PERIODIC 	(1 << 0)
#define TIMER_ONESHOT	(0 << 0)
#define TIMER_DONE	(1 << 1)
#define TIMER_ONTICK	(1 << 2)
#define TIMER_ACURATE	(1 << 3) /* TODO */

typedef void (timer_func_t) (void *arg);

typedef struct timer {
	list_node_t node;
	struct timerq *tq;

	int flags;
	nanosec_t time;
	nanosec_t period;

	timer_func_t *func;
	void *arg;
} timer_t;

static inline void timer_init(timer_t *timer, timer_func_t *func, void *arg) {
	list_node_init(timer, &timer->node);
	timer->func = func;
	timer->arg = arg;
}

static inline void timer_destroy(timer_t *timer) {
	list_node_destroy(&timer->node);
}

void timer_start(timer_t *timer, nanosec_t time, int flags);
void timer_ontick(timer_t *timer);
nanosec_t timer_stop(timer_t *timer);
void init_timer(void);

#endif