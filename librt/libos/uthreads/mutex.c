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

#include <assert.h>
#include <os/usched/job.h>
#include <os/usched/mutex.h>

#include "private.h"

void usched_mtx_init(struct usched_mtx* mutex, int type)
{
    assert(mutex != NULL);

    spinlock_init(&mutex->lock);
    mutex->type       = type;
    mutex->references = 0;
    mutex->owner      = NULL;
    mutex->queue      = NULL;
}

static int
__BlockAndWait(
    _InOut_ struct usched_mtx*              mutex,
    _In_    struct usched_job*              current,
    _In_    const struct timespec *restrict until)
{
    struct usched_wait wait;

    // mutex->lock must be held until the wait is queued, so an unlock
    // cannot miss it.
    __usched_wait_prepare(&wait, until);
    __usched_wait_enqueue(&mutex->queue, &wait);
    
    // release and yield to allow other jobs to acquire the mutex.
    spinlock_release(&mutex->lock);
    usched_job_yield();
    
    // A timed-out wait may still be queued. Remove it before returning.
    spinlock_acquire(&mutex->lock);
    __usched_wait_remove(&mutex->queue, &wait);
    if (wait.result == UschedWaitStatus_TIMEOUT) {
        errno = ETIME;
        return -1;
    }

    assert(mutex->owner == current);
    mutex->references = 1;
    return 0;
}

int usched_mtx_timedlock(struct usched_mtx* mutex, const struct timespec *restrict until)
{
    struct usched_job* current;
    int                status = 0;
    assert(mutex != NULL);

    current = __usched_get_execution()->current;
    assert(current != NULL);

    spinlock_acquire(&mutex->lock);
    if (mutex->owner) {
        // support recursive mutexes
        if (mutex->owner == current && (mutex->type & MUTEX_RECURSIVE)) {
            mutex->references++;
        } else if (__BlockAndWait(mutex, current, until) == -1) {
            errno  = ETIME;
            status = -1;
        }
    } else {
        mutex->owner      = current;
        mutex->references = 1;
    }
    spinlock_release(&mutex->lock);
    return status;
}

int usched_mtx_trylock(struct usched_mtx* mutex)
{
    struct usched_job* current;
    int                status = 0;
    assert(mutex != NULL);

    current = __usched_get_execution()->current;
    assert(current != NULL);

    spinlock_acquire(&mutex->lock);
    if (mutex->owner) {
        // support recursive mutexes
        if (mutex->owner == current && (mutex->type & MUTEX_RECURSIVE)) {
            mutex->references++;
        } else {
            errno  = EBUSY;
            status = -1;
        }
    } else {
        mutex->owner      = current;
        mutex->references = 1;
    }
    spinlock_release(&mutex->lock);
    return status;
}

void usched_mtx_lock(struct usched_mtx* mutex)
{
    int status = usched_mtx_timedlock(mutex, NULL);
    assert(status == 0);
}

void
usched_mtx_unlock(
    _InOut_ struct usched_mtx* mutex)
{
    struct usched_job* current;

    assert(mutex != NULL);
    current = __usched_get_execution()->current;
    assert(current != NULL);

    spinlock_acquire(&mutex->lock);
    assert(mutex->owner == current);
    mutex->references--;
    if (!mutex->references) {
        // Hand the mutex directly to the next waiting job, if any, and wake it up.
        mutex->owner = __usched_wait_signal(&mutex->queue);
    }
    spinlock_release(&mutex->lock);
}
