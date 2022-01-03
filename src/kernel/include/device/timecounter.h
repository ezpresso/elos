#ifndef DEVICE_TIMECOUNTER_H
#define DEVICE_TIMECOUNTER_H

typedef struct timecounter {
	struct timecounter *next;

	const char *name;
	frequency_t freq;
	uint64_t mask;
	int quality;
	void *priv;
	uint64_t (*read) (struct timecounter *);
} timecounter_t;

static inline void *tc_priv(timecounter_t *tc) {
	return tc->priv;
}

/**
 * @brief Register a new time counter.
 */
void tc_register(timecounter_t *tc);

/**
 * @brief Unregister a new time counter.
 */
int tc_unregister(timecounter_t *tc);

/**
 * @brief Read the counter of a time counter device.
 */
static inline uint64_t tc_read(timecounter_t *tc) {
	return tc->read(tc);
}

#endif