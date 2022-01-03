#ifndef KERN_TIME_H
#define KERN_TIME_H

#include <sys/time.h> /* struct timespec */

/**
 * @brief The number of nanoseconds per second.
 */
#define SEC_NANOSECS 1000000000LL

/**
 * @brief Convert seconds to nanoseconds.
 */
#define SEC2NANO(x) ((x) * SEC_NANOSECS)

/**
 * @brief Convert milliseconds to nanoseconds.
 */
#define MILLI2NANO(x) ((x) * 1000000LL)

/**
 * @brief Convert microseconds to nanoseconds.
 */
#define MICRO2NANO(x) ((x) * 1000LL)

/*
 * @brief The POSIX year base for real-time clocks.
 */
#define YEAR_BASE 1970

#define wait_for_ms(contition, timeout) \
	wait_for_ns(contition, MILLI2NANO(timeout))

#define wait_for_us(contition, timeout) \
	wait_for_ns(contition, MICRO2NANO(timeout))

#define wait_for_ns(condition, timeout) ({ 		\
	nanosec_t _end = nanouptime() + (timeout);	\
	bool _ret = false;				\
	do {						\
		if((condition)) {			\
			_ret = true;			\
			break;				\
		}					\
	} while(nanouptime() < _end);			\
	_ret;						\
})

typedef struct datetime {
	long nsec;
	int sec;
	int min;
	int hour;
	int day; /* Day of month */
	int mon; /* 1 -> Jan */
	int year;
} datetime_t;

/**
 * @brief Get the current time.
 */
void realtime(struct timespec *time);

nanosec_t timekeep_tick(void);

nanosec_t nanouptime(void);

nanosec_t getnanouptime(void);

void tsuptime(struct timespec *time);

void gettsuptime(struct timespec *time);

void gettsboottime(struct timespec *time);

void ndelay(nanosec_t nsec);

/**
 * @brief Suspend the current thread for a specific amount of time.
 *
 * @retval -EINTR 	The sleep was interrupted.
 * @retval -ERESTART	The sleep was interrupted and the syscall shall be
 *			restarted.
 * @retval 0		Success.
 */
int sleep_timespec(struct timespec *ts);

/**
 * @brief Suspend the current thread for @p millisec milliseconds.
 */
static inline int msleep(long millisec) {
	struct timespec ts = {
		.tv_sec = millisec / 1000,
		.tv_nsec = MILLI2NANO(millisec % 1000),
	};
	return sleep_timespec(&ts);
}

/**
 * @brief Convert a datetime to a timespec.
 *
 * @param[out] ts The resulting timespec.
 */
int datetime_to_ts(datetime_t *datetime, struct timespec *ts);

/**
 * @brief Add two timespecs.
 *
 * @param[out] res The resulting timespec.
 */
void ts_add(struct timespec *a, struct timespec *b, struct timespec *res);

/**
 * @brief Increment the time of a timespec.
 *
 * Adds the nanoseconds @p ns to the timespec @p ts.
 */
void ts_inc(struct timespec *ts, nanosec_t ns);

/**
 * @brief Convert nanoseconds into a struct timespec.
 */
static inline void nsec_to_ts(nanosec_t ns, struct timespec *ts) {
	ts->tv_sec = ns / SEC_NANOSECS;
	ts->tv_nsec = ns % SEC_NANOSECS;
}

/**
 * @brief Convert a struct timespec into nanoseconds.
 */
static inline nanosec_t ts_to_nsec(struct timespec *ts) {
	return (nanosec_t)ts->tv_sec * SEC_NANOSECS + ts->tv_nsec;
}

/**
 * @brief Copy a struct timespec from user into kernelspace.
 *
 * @retval -EFAULT The user pointer was invalid.
 * @retval -EINVAL The user timespec was invalid.
 * @retval 0	   Success.
 */
int copyin_ts(struct timespec *ts, const struct timespec *uts);

/**
 * @brief nanosleep syscall.
 */
int sys_nanosleep(const struct timespec *ureq, struct timespec *urem);

/**
 * @brief clock_gettime syscall.
 */
int sys_clock_gettime(clockid_t clk, struct timespec *res);

/**
 * @brief clock_getres syscall.
 */
int sys_clock_getres(clockid_t clk, struct timespec *ts);

void init_timekeep(void);

#endif
