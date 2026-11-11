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
#include <os/usched/cond.h>
#include <assert.h>

#include "private.h"

void
usched_cnd_init(
    _InOut_ struct usched_cnd* condition)
{
    assert(condition != NULL);
    spinlock_init(&condition->lock);
    condition->queue = NULL;
}

int
usched_cnd_timedwait(
    _InOut_ struct usched_cnd*              condition,
    _InOut_ struct usched_mtx*              mutex,
    _In_    const struct timespec *restrict until)
{
    struct usched_wait wait;

    assert(condition != NULL);
    assert(mutex != NULL);

    spinlock_acquire(&condition->lock);
    __usched_wait_prepare(&wait, until);
    __usched_wait_enqueue(&condition->queue, &wait);
    spinlock_release(&condition->lock);

    usched_mtx_unlock(mutex);
    usched_job_yield();

    // A timeout can leave the wait in the queue. Remove it before this
    // stack frame is released.
    spinlock_acquire(&condition->lock);
    __usched_wait_remove(&condition->queue, &wait);
    spinlock_release(&condition->lock);
    
    usched_mtx_lock(mutex);
    if (wait.result == UschedWaitStatus_TIMEOUT) {
        errno = ETIME;
        return -1;
    }
    return 0;
}

void
usched_cnd_wait(
    _InOut_ struct usched_cnd* condition,
    _InOut_ struct usched_mtx* mutex)
{
    (void)usched_cnd_timedwait(condition, mutex, NULL);
}

void
usched_cnd_notify_one(
    _InOut_ struct usched_cnd* condition)
{
    assert(condition != NULL);

    spinlock_acquire(&condition->lock);
    (void)__usched_wait_signal(&condition->queue);
    spinlock_release(&condition->lock);
}

void
usched_cnd_notify_all(
    _InOut_ struct usched_cnd* condition)
{
    assert(condition != NULL);

    spinlock_acquire(&condition->lock);
    for (;;) {
        if (__usched_wait_signal(&condition->queue) == NULL) {
            break;
        }
    }
    spinlock_release(&condition->lock);
}
