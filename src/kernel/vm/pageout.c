/*
 * ███████╗██╗      ██████╗ ███████╗
 * ██╔════╝██║     ██╔═══██╗██╔════╝
 * █████╗  ██║     ██║   ██║███████╗
 * ██╔══╝  ██║     ██║   ██║╚════██║
 * ███████╗███████╗╚██████╔╝███████║
 * ╚══════╝╚══════╝ ╚═════╝ ╚══════╝
 *
 * Copyright (c) 2018, Elias Zell
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
#include <kern/proc.h>
#include <kern/init.h>
#include <kern/sched.h>
#include <kern/futex.h>
#include <kern/time.h>
#include <vm/pageout.h>
#include <vm/object.h>
#include <vm/pager.h>
#include <vm/page.h>
#include <vm/phys.h>
#include <vm/vas.h>
#include <vm/mmu.h>
#include <vm/pressure.h>
#include <lib/list.h>
#include <sys/limits.h>

#define VM_GEN_SYNC		1
#define VM_GEN_INACT		16

#define VM_PGOUT_DELAY_M	50 /* delay when memory pressure is moderate */
#define VM_PGOUT_DELAY_L	200 /* delay when memory pressure is very low */
#define VM_SYNC_DELAY		160
#define VM_SYNCQ_DELAY		(VM_SYNC_DELAY / VM_SYNC_DELAY)

#define VM_NSYNCQ		32
#define VM_SYNCQ_MASK		(VM_NSYNCQ - 1)

thread_t *vm_pgout_thread;
static list_t vm_syncq[VM_NSYNCQ];
static size_t vm_syncq_idx = 0;
/* TODO currently not used */
static size_t vm_nsync = 0;

static sync_t vm_pageout_lock = SYNC_INIT(MUTEX);
static DEFINE_LIST(vm_inactive);
static DEFINE_LIST(vm_active);

bool vm_is_pageout(void) {
	return cur_thread() == vm_pgout_thread;
}

static bool vm_pageout_remove_page(vm_page_t *page, vm_pgstate_t state) {
	sync_assert(&vm_pageout_lock);
	if(state == VM_PG_PGOUT) {
		list_remove(&vm_active, &page->pgout_node);
		return true;
	} else if(state == VM_PG_INACTIVE) {
		list_remove(&vm_inactive, &page->pgout_node);
		return true;
	} else {
		return false;
	}
}

void vm_pageout_pin(vm_page_t *page) {
	sync_scope_acquire(&vm_pageout_lock);
	if(vm_pageout_remove_page(page, vm_page_state(page))) {
		vm_page_set_state(page, VM_PG_PINNED);
	}
}

void vm_pageout_unpin(vm_page_t *page) {
	vm_page_assert_not_busy(page);

	sync_scope_acquire(&vm_pageout_lock);
	if(vm_page_state(page) == VM_PG_PINNED) {
		vm_page_set_state(page, VM_PG_PGOUT);
		list_append(&vm_active, &page->pgout_node);
	}
}

void vm_pageout_add(vm_page_t *page) {
	vm_page_assert_pinned(page);
	vm_page_set_state(page, VM_PG_PINNED);
}

static void vm_pageout_page_wait(vm_page_t *page) {
	vm_pgflags_t flags;

	vm_page_assert_pinned(page);
	sync_assert(&vm_pageout_lock);

	/*
	 * Wait while pgout is tampering around with that page.
	 */
	while((flags = vm_page_flags(page) & VM_PG_STATE_MASK) !=
		VM_PG_PINNED)
	{
		sync_release(&vm_pageout_lock);
		kern_wait(&page->flags, flags, 0);
		sync_acquire(&vm_pageout_lock);
	}
}

bool vm_pageout_rem(vm_object_t *object, vm_page_t *page) {
	sync_assert(&object->lock);

	/*
	 * Pageout doesn't use this page.
	 */
	if(vm_page_state(page) == VM_PG_NORMAL) {
		return false;
	}

	/*
	 * Prevent pageout from freeing the page.
	 */
	vm_page_pin(page);
	sync_release(&object->lock);

	synchronized(&vm_pageout_lock) {
		if(vm_page_state(page) == VM_PG_SYNCQ) {
			list_remove(&vm_syncq[page->syncq_idx],
				&page->pgout_node);
		} else {
			/*
			 * Wait while pgout is tampering around with that page.
			 */
			vm_pageout_page_wait(page);
		}

		/*
		 * Indicate that the page it is no longer a part of
		 * pageout. Otherwise the unpin below would put the
		 * page back onto the pageout queue.
		 */
		vm_page_set_state(page, VM_PG_NORMAL);
	}

	sync_acquire(&object->lock);
	vm_page_unpin(page);

	return true;
}

void vm_pageout_done(vm_page_t *page, int err) {
	/*
	 * We can use vm_page_object safely here, because the page is currently
	 * busy and vm_object_pages_migrate(), which is capable of changing
	 * page->object, waits until the page is not busy anymore. Furhtermore
	 * the object is locked before unbusying so everything should be fine.
	 */
	vm_object_t *object = vm_page_object(page);
	vm_pgstate_t state;
	uint16_t pincnt;

	state = vm_page_state(page);

	sync_acquire(&object->lock);
	pincnt = vm_page_pincnt(page);

	/*
	 * Mark the page as being clean, if it was sucessfully
	 * written to disk.
	 *
	 * Since the page is busy and any fault wanting that
	 * page will currently wait for unbusy, there should
	 * not be any race between this vm_page_clean() and
	 * vm_page_dirty().
	 */
	if(err == 0) {
		vm_page_clean(page);
	}

	/*
	 * If the page was just synced (see vnode), don't
	 * necesserily free it.
	 */
	if(err || pincnt > 0 || (state == VM_PG_SYNC &&
			vm_pressure(VM_PR_PHYS) <= VM_PR_MODERATE))
	{
		sync_release(&object->lock);
		vm_page_unbusy(page);

		sync_scope_acquire(&vm_pageout_lock);
		if(pincnt) {
			vm_page_set_state(page, VM_PG_PINNED);
			kern_wake(&page->flags, INT_MAX, 0);
		} else {
			vm_page_set_state(page, VM_PG_PGOUT);
			list_append(&vm_active, &page->pgout_node);
		}
	} else {
		/*
		 * The page is not mapped anywhere and it's clean (i.e. written
		 * to disk). Thus it's possible to free the page.
		 */
		vm_object_page_remove(object, page);
		sync_release(&object->lock);
		vm_page_unbusy(page);
		vm_page_set_state(page, VM_PG_NORMAL);
		vm_page_free(page);
	}
}

void vm_sync_needed(vm_page_t *page, vm_sync_time_t time) {
	vm_pgstate_t state;
	size_t idx;

	vm_page_assert_pinned(page);
	kassert(vm_page_state(page) != VM_PG_NORMAL, "[vm] pageout: "
		"invalid page state: %d", vm_page_state(page));

	sync_scope_acquire(&vm_pageout_lock);
	state = vm_page_state(page);
	if(state == VM_PG_SYNCQ) {
		list_remove(&vm_syncq[page->syncq_idx], &page->pgout_node);
	} else if(state == VM_PG_SYNC) {
		return;
	} else {
		/*
		 * Wait until pageout is done with that page.
		 */
		vm_pageout_page_wait(page);
	}

	/*
	 * Maybe pageout cleaned the page while waiting.
	 */
	if(vm_page_is_dirty(page)) {
		vm_nsync++;
		vm_page_set_state(page, VM_PG_SYNCQ);

		/*
		 * Add the page to the sync queue.
		 */
		if(time == VM_SYNC_NOW) {
			idx = vm_syncq_idx;
		} else {
			idx = (vm_syncq_idx + (VM_NSYNCQ - 1)) & VM_SYNCQ_MASK;
		}

		list_append(&vm_syncq[idx], &page->pgout_node);
		page->syncq_idx = idx;
	}
}

static bool vm_pageout_choose(vm_pressure_t pr, vm_page_t **pagep) {
	vm_pgstate_t state = VM_PG_SYNC;
	vm_page_t *page;

	sync_acquire(&vm_pageout_lock);
	page = list_pop_front(&vm_syncq[vm_syncq_idx]);
	if(page) {
		vm_nsync--;
	}

	if(page == NULL && pr >= VM_PR_MODERATE) {
		state = VM_PG_LAUNDRY;

		/*
		 * The most inactive page is at the beginning of the inactive
		 * list. TODO add doc to vm_inactive_update
		 */
		page = list_pop_front(&vm_inactive);

		/*
		 * When the system is running out of memory, consider swapping
		 * out active pages.
		 */
		if(page == NULL && pr == VM_PR_HIGH) {
			page = list_pop_front(&vm_active);
		}
	}

	if(page) {
		/*
		 * Indicate that the page is currently being used by pageout.
		 */
		vm_page_set_state(page, state);
		sync_release(&vm_pageout_lock);
		return *pagep = page, true;
	} else {
		sync_release(&vm_pageout_lock);
		return false;
	}
}

static bool vm_pageout_page(vm_pressure_t pr) {
	vm_object_t *object;
	vm_page_t *page;
	int err;

	while(vm_pageout_choose(pr, &page) == true) {
		object = vm_page_lock_object(page);
		if(vm_page_pincnt(page) != 0) {
			vm_page_pin(page);
			synchronized(&vm_pageout_lock) {
				vm_page_set_state(page, VM_PG_PINNED);
			}
			sync_release(&object->lock);

			kern_wake(&page->flags, INT_MAX, 0);
			vm_page_unpin(page);
			continue;
		}

		err = vm_pager_pageout(object, page);
		if(err || !vm_page_is_dirty(page)) {
			sync_release(&object->lock);
			vm_pageout_done(page, err);
		} else {
			sync_release(&object->lock);
		}

		return err == 0;
	}

	return false;
}

static void vm_inactive_update(void) {
	vm_page_t *page;

	sync_scope_acquire(&vm_pageout_lock);
	page = list_pop_front(&vm_active);
	if(page) {
		vm_page_flag_set(page, VM_PG_INACTIVE);
		list_append(&vm_inactive, &page->pgout_node);
	}
}

static __used int vm_pageout(__unused void *arg) {
	size_t generation = 0;
	vm_pressure_t pr;

	/* TODO this is very temporary */
	while(true) {
		bool pgout;

		pr = vm_pressure(VM_PR_PHYS);
		pgout = vm_pageout_page(pr);
		generation++;

		/* TODO do this a little bit nicer... */
		sync_acquire(&vm_pageout_lock);
		if(!list_is_empty(&vm_syncq[vm_syncq_idx])) {
			continue;
		}
		sync_release(&vm_pageout_lock);

		if(pgout == false) {
			/*
			 * No pageout was done.
			 */
			msleep(VM_SYNCQ_DELAY);
		} else if(pr == VM_PR_MODERATE) {
			msleep(VM_PGOUT_DELAY_M);
		} else if(pr == VM_PR_LOW) {
			msleep(VM_PGOUT_DELAY_L);
		}

		/*
		 * Increment the sync-queue-index every now and then.
		 */
		if((generation % VM_GEN_SYNC) == 0) {
			sync_scope_acquire(&vm_pageout_lock);
			if(list_is_empty(&vm_syncq[vm_syncq_idx])) {
				vm_syncq_idx = (vm_syncq_idx + 1) &
					VM_SYNCQ_MASK;
			}
		}

		if((generation % VM_GEN_INACT) == 0) {
			vm_inactive_update();
		}
	}

	notreached();
}

void __init vm_pageout_init(void) {
	for(size_t i = 0; i < VM_NSYNCQ; i++) {
		list_init(&vm_syncq[i]);
	}
}

void __init vm_pageout_launch(void) {
	vm_pgout_thread = kthread_spawn_prio(vm_pageout, NULL, SCHED_KERNEL);
}
