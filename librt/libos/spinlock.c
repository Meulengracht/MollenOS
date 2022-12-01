/**
 * Copyright 2022, Philip Meulengracht
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
 */

#include <assert.h>
#include <os/spinlock.h>

// For the X86 platform we need the _mm_pause intrinsinc to generate
// a pause in-between each cycle. This is recommended by the AMD manuals.
#include <immintrin.h>

void 
spinlock_init(
	_In_ spinlock_t* lock)
{
    assert(lock != NULL);
    atomic_store(&lock->current, 0);
    atomic_store(&lock->next,    0);
}

int
spinlock_try_acquire(
	_In_ spinlock_t* lock)
{
    unsigned int current, next;
    assert(lock != NULL);

    // In order to test the spinlock, we try to first check that
    // current == next. If they match, then the spinlock is free and
    // we can immediately grap it
    current = atomic_load(&lock->current);
    next = atomic_load(&lock->next);
    if (current != next) {
        return spinlock_busy;
    }

    // We can still fail if someone comes in between here. So carefully try
    // to get the next ticket, if the ticket has been taken while we tried this
    // then this call will fail, and so will we.
    if (!atomic_compare_exchange_strong(&lock->next, &next, next + 1)) {
        return spinlock_busy;
    }
    return spinlock_acquired;
}

void
spinlock_acquire(
	_In_ spinlock_t* lock)
{
    unsigned int ticket;

    assert(lock != NULL);

    ticket = atomic_fetch_add(&lock->next, 1);
    for (;;) {
        unsigned int current = atomic_load(&lock->current);
        if (current == ticket) {
            break;
        } else if (current > ticket) {
            // Protect the thread against cases where the spinlock has been
            // released to many times. If the current ticket suddenly surpassed
            // our position in the queue, then something has definitely gone to
            // shit.
            // 1. We could do an assert here assert(current < ticket) and let the
            //    program itself crash
            // 2. We can ignore it and autocorrect by letting this thread get a new
            //    position in the queue.
            ticket = atomic_fetch_add(&lock->next, 1);
        }

        // As recommended by AMD we insert a pause instruction to
        // not tie up the cpu bus. If we are a single core CPU we should
        // definitely not spin here, and instead yield().
        // TODO implement yield here if we detected single CPU.
        // Another idea is to spin for a certain amount of times before
        // giving up and yielding. This will cause an increase in latency, but
        // should allow for less time spent spinning.
        // TODO we should test this
        _mm_pause();
    }
}

void
spinlock_release(
	_In_ spinlock_t* lock)
{
    int current;
    assert(lock != NULL);

    current = atomic_fetch_add(&lock->current, 1);

    // What we do here is try to detect multi-release
    // issues, which can be quite fatal for a program.
    // The problem with this detection is that it does not
    // detect if a thread releases multiple times and there
    // are enough waiters in queue to cover the additional
    // releases
    if (current >= atomic_load(&lock->next)) {
        BOCHSBREAK;
        return;
    }
    assert(current < atomic_load(&lock->next));
}
