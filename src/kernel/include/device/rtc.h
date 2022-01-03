#ifndef DEVICE_RTC_H
#define DEVICE_RTC_H

#include <lib/list.h>

#define RTC_ERROR	-1
#define RTC_OK		0

struct timespec;

typedef struct rtcdev {
	list_node_t node;

	nanosec_t resolution;
	int (*gettime) (struct rtcdev *, struct timespec *ts);

#if notyet
	int (*settime) (struct rtcdev *, ...);
#endif
} rtcdev_t;

/**
 * @brief Register a real time clock.
 */
void rtc_register(rtcdev_t *rtc);

/**
 * @brief Unregister a real time clock.
 */
void rtc_unregister(rtcdev_t *rtc);

/**
 * @brief Get the real time of the real time clock in the system.
 *
 * @param[out] ts The real time.
 */
void rtc_time(struct timespec *ts);

#endif