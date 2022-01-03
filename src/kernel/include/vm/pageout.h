#ifndef VM_PAGEOUT_H
#define VM_PAGEOUT_H

struct vm_page;
struct vm_object;

typedef enum vm_sync_time {
	VM_SYNC_NORMAL,
	VM_SYNC_NOW,
} vm_sync_time_t;

/**
 * @brief Make sure a page is written to disk in the near future.
 *
 * Some pages, like the ones of vnodes, in the system need to be written
 * to disk regularly, if they are dirty of course. This function makes sure
 * that those pages are written to disk, by the pageout thread after a certain
 * delay. @p page should have been pinned by the caller.
 *
 * @param page The page that needs to be synced.
 * @param time The timing of the sync.
 */
void vm_sync_needed(struct vm_page *page, vm_sync_time_t time);

/**
 * @brief Internal function, do not use directly.
 *
 * Inform the pageout subsystem, that a page was pinned. As a result,
 * the page cannot be freed by pageout.
 *
 * @page page The page that was pinned.
 */
void vm_pageout_pin(struct vm_page *page);

/**
 * @brief Internal function, do not use directly.
 *
 * Inform the pageout subsystem, that a page is now unpinned. As a result
 * the page becomes "pageoutable".
 *
 * @page page The page.
 */
void vm_pageout_unpin(struct vm_page *page);

/**
 * @brief A pageout request finished.
 *
 * When the pageout thread wants to pageout a page, it usually starts
 * an asynchronous block-device I/O request. This function handles
 * a finished request
 *
 * @param page 	The page that was written to disk.
 * @param error If this value is nonzero, an error happend during I/O.
 */
void vm_pageout_done(struct vm_page *page, int error);

/**
 * @brief Internal function, do not use directly.
 *
 * Called by vm_object_page_alloc() _only_, to register the page
 * in the pageout subsystem. The page has to be pinned by the caller.
 */
void vm_pageout_add(struct vm_page *page);

/**
 * @brief Remove a page from pageout.
 *
 * Make sure that a page cannot be used by pageout anymore. To achieve
 * this, the object might need to be unlocked while waiting for a
 * started pageout-request.
 *
 * @brief object	The object the desired page belongs to. The caller needs
 *			to hold the lock of the object.
 * @brief page		The page being removed from pageout.
 *
 * @retval false 	Did not have to wait and thus the object was not unlocked
 * @retval true		Had to unlock the object and wait. 
 */
bool vm_pageout_rem(struct vm_object *object, struct vm_page *page);

/**
 * @brief Check if the current thread is the pageout thread.
 *
 * @retval true  The current thread is the pageout thread.
 * @retval false The current thread is not the pageout thread.
 */
bool vm_is_pageout(void);

/**
 * @brief Initialize the pageout subsystem.
 *
 * Initialize the pageout subsystem, but don't yet spawn the pageout
 * thread.
 */
void vm_pageout_init(void);

/**
 * @brief Start the pageout thread.
 */
void vm_pageout_launch(void);

#endif