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
#include <kern/proc.h>
#include <kern/atomic.h>
#include <kern/futex.h>
#include <kern/sched.h>
#include <device/intr.h>

static int intr_thread(void *arg) {
	intr_src_t *src = arg;
	bus_res_t *hand;
	waiter_t wait;
	int err;

	waiter_init(&wait);
	for(;;) {
		wait_prep(&src->wq, &wait);

		/*
		 * Call all of the interrupt handlers.
		 */
		while(atomic_xchg(&src->pending, 0) == 1) {
			sync_scope_acquire(&src->mtx);
			foreach(hand, &src->handlers) {
				if(hand->intr.thand) {
					hand->intr.thand(hand->intr.arg);
				}
			}
		}

		/*
		 * Will be woken up again when another interrupt occurs, or
		 * this thread should terminate.
		 */
		err = wait_sleep(&src->wq, &wait, WAIT_INTERRUPTABLE);
		if(err) {
			/*
			 * the thread was killed...
			 */
			break;
		}
	}

	waiter_destroy(&wait);
	if(atomic_dec_relaxed(&src->cntlr->nthr) == 1) {
		kern_wake(&src->cntlr->nthr, 1, 0);
	}

	/*
	 * Bye, bye.
	 */
	return 0;
}

void intr_cntlr_init(intr_cntlr_t *cntlr, intr_src_t *intrs, intnum_t nintr) {
	cntlr->intrs = intrs;
	cntlr->nintr = nintr;

	for(intnum_t i = 0; i < nintr; i++) {
		intr_src_t *src = &intrs[i];
		sync_init(&src->lock, SYNC_SPINLOCK);
		sync_init(&src->mtx, SYNC_MUTEX);
		list_init(&src->handlers);
		waitqueue_init(&src->wq);
		src->cntlr = cntlr;
		src->intr = i;
		src->flags = 0;
		src->pending = 0;
		src->num = 0;
		src->nthr = 0;
		src->ithr = NULL;
	}
}

void intr_cntlr_destroy(intr_cntlr_t *cntlr) {
	/*
	 * Make sure that every interrupt thread terminated.
	 */
	for(;;) {
		size_t nthread = atomic_load(&cntlr->nthr);
		if(nthread == 0) {
			break;
		} else {
			kern_wait(&cntlr->nthr, nthread, 0);
		}
	}

	for(intnum_t i = 0; i < cntlr->nintr; i++) {
		intr_src_t *src = &cntlr->intrs[i];

		assert(src->num == 0);
		assert(src->ithr == NULL);

		sync_destroy(&src->mtx);
		sync_destroy(&src->lock);
		list_destroy(&src->handlers);
		waitqueue_destroy(&src->wq);
	}
}

void intr_config(intr_cntlr_t *cntlr, intnum_t intr, int flags) {
	intr_src_t *src = intr_src(cntlr, intr);

	kassert((flags & ~(BUS_INTR_TRIG_MASK | BUS_INTR_POL_MASK |
		BUS_INTR_SHARED)) == 0, "[intr] config: invalid flags: %d",
		flags);
	assert(list_length(&src->handlers) == 0);
	src->flags = flags;
}

void intr_reserve(intr_cntlr_t *cntlr, intnum_t intr) {
	intr_src_t *src;

	assert(intr < cntlr->nintr);
	src = &cntlr->intrs[intr];
	assert(src->num == 0);

	src->flags = 0; /* Not shareable */
	src->num = 1;
}

int intr_alloc(intr_cntlr_t *cntlr, bus_res_req_t *req) {
	intr_src_t *best = NULL, *cur;
	bus_res_t *res = req->res;
	int trig, pol;
	intnum_t i;

	sync_assert(&device_lock);
	if(req->type != BUS_RES_INTR || req->bus_id != 0 || req->align != 1) {
		return -EINVAL;
	} else if(req->size != 1) {
		kpanic("[intr] allocating multiple interrupts at once not "
			"supported\n");
	}

	/*
	 * Don't allow shared edge triggered interrupts for now.
	 */
	if(F_ISSET(res->intr.flags, BUS_INTR_SHARED) &&
		BUS_IS_TRIG_EDGE(res->intr.flags))
	{
		kpanic("[intr] shared edge triggered interrupt");
	}

	trig = res->intr.flags & BUS_INTR_TRIG_MASK;
	pol = res->intr.flags & BUS_INTR_POL_MASK;

	/*
	 * Figure out an interrupt which fits the needs.
	 */
	req->end = min(cntlr->nintr - 1, req->end);
	for(i = req->addr; i <= req->end; i++) {
		cur = intr_src(cntlr, i);

		if(((cur->flags & BUS_INTR_TRIG_MASK) != trig ||
			(cur->flags & BUS_INTR_POL_MASK) != pol))
		{
			/*
			 * Check if this interrupt can be configured the way
			 * requested. Remember to not unmask the interrupt
			 * yet.
			 */
			if(cur->num == 0 && cntlr->config(cntlr, i,
				res->intr.flags | BUS_INTR_MASKED))
			{
				intr_config(cntlr, i, res->intr.flags);
				best = cur;
				break;
			} else {
				/*
				 * The interrupt cannot be allocated here.
				 */
				continue;
			}
		} else {
			/*
			 * The polarity and the trigger mode match. See if this
			 * interrupt source supports shared interrupts.
			 */
			if(cur->num && (!(res->intr.flags & BUS_INTR_SHARED) ||
				!(cur->flags & BUS_INTR_SHARED)))
			{
				continue;
			}

			if(best == NULL || cur->num < best->num) {
				/*
				 * If multiple interrupt sources are possible,
				 * try to avoid too much sharing.
				 */
				best = cur;
			}
		}
	}

	if(best == NULL) {
		return -ENOSPC;
	}

	best->num++;
	bus_res_init(res, best->intr, 1);

	return 0;
}

int intr_free(intr_cntlr_t *cntlr, bus_res_t *res) {
	sync_assert(&device_lock);
	intr_src(cntlr, bus_res_get_addr(res))->num--;
	bus_res_destroy(res);
	return 0;
}

void intr_add_handler(intr_cntlr_t *cntlr, bus_res_t *res) {
	intr_src_t *src = intr_src(cntlr, bus_res_get_addr(res));

	sync_assert(&device_lock);
	sync_acquire(&src->mtx);
	if(res->intr.thand) {
		/*
		 * Spawn a new interrupt thread if necessary.
		 */
		if(src->ithr == NULL) {
			src->ithr = kthread_spawn_prio(intr_thread, src,
				SCHED_INTR);
		}

		src->nthr++;
	}

	sync_acquire(&src->lock);
	list_append(&src->handlers, &res->intr.node);
	sync_release(&src->lock);
	sync_release(&src->mtx);

	/*
	 * Unmask the interrupt by configuring the interrupt without
	 * the mask bit being set.
	 */
	if(list_length(&src->handlers) == 1) {
		cntlr->config(cntlr, src->intr, src->flags);
	}
}

void intr_remove_handler(intr_cntlr_t *cntlr, bus_res_t *res) {
	intr_src_t *src = intr_src(cntlr, bus_res_get_addr(res));

	sync_assert(&device_lock);

	/*
	 * Mask the interrupt, if there are no more handlers.
	 */
	if(list_length(&src->handlers) == 1) {
		cntlr->config(cntlr, src->intr, src->flags | BUS_INTR_MASKED);
	}

	sync_acquire(&src->mtx);
	sync_acquire(&src->lock);
	list_remove(&src->handlers, &res->intr.node);
	sync_release(&src->lock);

	/*
	 * Kill interrupt thread if necessary.
	 */
	if(res->intr.thand && --src->nthr == 0) {
		thread_kill(src->ithr);
	}
	sync_release(&src->mtx);
}

int intr_handle(intr_cntlr_t *cntlr, intnum_t intr) {
	intr_src_t *src = intr_src(cntlr, intr);
	int result = BUS_INTR_STRAY, ret;
	bus_res_t *hand;
	bool ithr = false;

	sync_scope_acquire(&src->lock);
	foreach(hand, &src->handlers) {
		if(hand->intr.hand) {
			/*
			 * TODO this assumes that the normal interrupt handler
			 * stops this interrupt. Thus the interrupt is not
			 * masked when there is a thread handler.
			 */
			ret = hand->intr.hand(hand->intr.arg, intr);
		} else if(hand->intr.thand) {
			/*
			 * TODO the interrupt would need to be masked, because
			 * there is no normal handler, which could stop the
			 * intr.
			 */
			kpanic("TODO mask intr");
			ret = BUS_INTR_ITHR;
		} else {
			continue;
		}

		if(ret == BUS_INTR_OK) {
			result = BUS_INTR_OK;

			/*
			 * Only probe until one device handled the level
			 * triggered interrupt. If there are multiple devices
			 * requesting the same interrupt, the handler will be
			 * called again since it's a level triggered interrupt.
			 * When an edge triggered interrupt is requested, all
			 * of the handlers have to be called though.
			 */
			if(BUS_IS_TRIG_LEVEL(src->flags)) {
				result = ret;
				break;
			}
		} else if(ret == BUS_INTR_ITHR) {
			assert(hand->intr.thand);
			ithr = true;
		}
	}

	if(ithr == true) {
		atomic_store(&src->pending, 1);
		assert(src->ithr);

		/*
		 * Schedule interrupt thread.
		 */
		wakeup(&src->wq, SCHED_INTR);
	}

	return result;
}
