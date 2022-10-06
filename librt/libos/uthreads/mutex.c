/**
 * Copyright 2021, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <os/usched/job.h>
#include <os/usched/mutex.h>
#include <assert.h>
#include "private.h"

void usched_mtx_init(struct usched_mtx* mutex)
{
    assert(mutex != NULL);

    spinlock_init(&mutex->lock);
    mutex->owner = NULL;
    mutex->queue = NULL;
}

static int BlockAndWait(
        struct usched_mtx*              mutex,
        struct usched_job*              current,
        const struct timespec *restrict until)
{
    int status = 0;
    int timer;

    // set us blocked
    current->state = JobState_BLOCKED;

    // add us to the blocked queue
    __usched_append_job(&mutex->queue, current);

    // wait for ownership
    while (mutex->owner != current) {
        spinlock_release(&mutex->lock);
        if (until != NULL) {
            union usched_timer_queue queue = { .mutex = mutex };
            timer = __usched_timeout_start(until, &queue, __QUEUE_TYPE_MUTEX);
        }
        usched_job_yield();
        if (until != NULL) {
            status = __usched_timeout_finish(timer);
        }
        spinlock_acquire(&mutex->lock);
    }
    return status;
}

int usched_mtx_timedlock(struct usched_mtx* mutex, const struct timespec *restrict until)
{
    struct usched_job* current;
    int                status = 0;
    assert(mutex != NULL);

    current = __usched_get_scheduler()->current;
    assert(current != NULL);

    spinlock_acquire(&mutex->lock);
    assert(mutex->owner != current);
    if (mutex->owner) {
        if (BlockAndWait(mutex, current, until) == -1) {
            errno  = ETIME;
            status = -1;
        }
    } else {
        mutex->owner = current;
    }
    spinlock_release(&mutex->lock);
    return status;
}

int usched_mtx_trylock(struct usched_mtx* mutex)
{
    struct usched_job* current;
    int                status;
    assert(mutex != NULL);

    current = __usched_get_scheduler()->current;
    assert(current != NULL);

    spinlock_acquire(&mutex->lock);
    if (mutex->owner) {
        errno  = EBUSY;
        status = -1;
    } else {
        mutex->owner = current;
        status       = 0;
    }
    spinlock_release(&mutex->lock);
    return status;
}

void usched_mtx_lock(struct usched_mtx* mutex)
{
    int status = usched_mtx_timedlock(mutex, NULL);
    assert(status == 0);
}

void usched_mtx_unlock(struct usched_mtx* mutex)
{
    struct usched_job* current;
    struct usched_job* next;
    assert(mutex != NULL);

    current = __usched_get_scheduler()->current;
    assert(current != NULL);

    spinlock_acquire(&mutex->lock);
    assert(mutex->owner == current);

    next = mutex->queue;
    mutex->owner = next;
    if (next) {
        mutex->queue = next->next;
        next->next = NULL;
    }
    spinlock_release(&mutex->lock);

    // mark next job as woken
    if (next) {
        next->state = JobState_RUNNING;
        __usched_add_job_ready(next);
    }
}

void __usched_mtx_notify_job(struct usched_mtx* mtx, struct usched_job* job)
{
    assert(mtx != NULL);
    assert(job != NULL);

    spinlock_acquire(&mtx->lock);
    if (mtx->queue) {
        struct usched_job* i = mtx->queue, *previous = NULL;
        while (i) {
            if (i == job) {
                if (!previous) {
                    mtx->queue = i->next;
                }
                else {
                    previous->next = i->next;
                }

                job->next = NULL;
                job->state = JobState_RUNNING;
                __usched_add_job_ready(job);
                break;
            }

            previous = i;
            i = i->next;
        }
    }
    spinlock_release(&mtx->lock);
}
