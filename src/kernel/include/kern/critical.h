#ifndef KERN_CRITICAL_H
#define KERN_CRITICAL_H

#define assert_not_critsect(msg) \
	kassert(!critsect_p(), msg)

#define assert_critsect(msg) \
	kassert(critsect_p(), msg)

#define assert_critsect_level(lvl, msg) \
	kassert(critsect_p() && critsect_level() == lvl, msg)

#define critical_internal(i) \
	for(__cleanup(critical_cleanup) int i = ({ critical_enter(); 0; }); \
		i == 0; i++)

/**
 * @brief Open a new cirtical section scope.
 *
 * Open a new cirtical section scope. The critical section
 * will be automatically exited when exiting the scope.
 */
#define critical critical_internal(UNIQUE_NAME(__crit_cntr))

/**
 * @brief Get the current critical section nesting count.
 */
size_t critsect_level(void);

/**
 * @brief Check if the current thread is inside a critical section.
 */
bool critsect_p(void);

/**
 * @brief Enter a critical section.
 *
 * Enter a critical section by disabling interrupts on the calling cpu.
 * Nesting is supported, which means 2 critical_leave() will undo 2
 * critical_enter().
 */
void critical_enter(void);

/**
 * @brief Extit a critical secton.
 */
void critical_leave(void);

static inline void critical_cleanup(__unused int *not_needed) {
	critical_leave();
}

#endif