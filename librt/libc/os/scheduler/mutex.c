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

#include <os/usched/usched.h>
#include <os/usched/mutex.h>
#include <assert.h>
#include "private.h"

void usched_mtx_init(struct usched_mtx* mutex)
{
    assert(mutex != NULL);

    spinlock_init(&mutex->lock, spinlock_plain);
    mutex->owner = NULL;
    mutex->queue = NULL;
}

static void BlockAndWait(struct usched_mtx* mutex, struct usched_job* current)
{
    // set us blocked
    current->state = JobState_BLOCKED;

    // add us to the blocked queue
    __usched_append_job(&mutex->queue, current);

    // wait for ownership
    while (mutex->owner != current) {
        spinlock_release(&mutex->lock);
        usched_yield();
        spinlock_acquire(&mutex->lock);
    }
}

void usched_mtx_lock(struct usched_mtx* mutex)
{
    struct usched_job* current;
    assert(mutex != NULL);

    current = __usched_get_scheduler()->current;
    assert(current != NULL);

    spinlock_acquire(&mutex->lock);
    assert(mutex->owner != current);
    if (mutex->owner) {
        BlockAndWait(mutex, current);
    }
    else {
        mutex->owner = current;
    }
    spinlock_release(&mutex->lock);
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
