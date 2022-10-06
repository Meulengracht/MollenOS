/**
 * MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 *
 * Synchronization (Irq Spinlocks)
 * - Spinlock implementation that also disables interrupts.
 */

#include <arch/interrupts.h>
#include <assert.h>
#include <spinlock.h>

// For the X86 platform we need the _mm_pause intrinsinc to generate
// a pause in-between each cycle. This is recommended by the AMD manuals.
#include <immintrin.h>

void
SpinlockConstruct(
        _In_ Spinlock_t* spinlock)
{
    assert(spinlock != NULL);

    spinlock->IrqState = 0;
    atomic_store(&spinlock->Current, 0);
    atomic_store(&spinlock->Next,    0);
}

void
SpinlockAcquire(
        _In_ Spinlock_t* spinlock)
{
    unsigned int ticket;
    assert(spinlock != NULL);

    ticket = atomic_fetch_add(&spinlock->Next, 1);
    for (;;) {
        unsigned int current = atomic_load(&spinlock->Current);
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
            ticket = atomic_fetch_add(&spinlock->Next, 1);
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
SpinlockAcquireIrq(
        _In_ Spinlock_t* spinlock)
{
    irqstate_t irqState;

    assert(spinlock != NULL);

    // Retrieve the current state before we enter into the spinlock
    // acquire loop.
    irqState = InterruptDisable();

    // Re-use the default implementation, but now in a irq-safe context.
    SpinlockAcquire(spinlock);

    // If we reach here, the lock is ours, store the state
    spinlock->IrqState = irqState;
}

void
SpinlockRelease(
        _In_ Spinlock_t* spinlock)
{
    unsigned int current;
    assert(spinlock != NULL);

    current = atomic_fetch_add(&spinlock->Current, 1);

    // What we do here is try to detect multi-release
    // issues, which can be quite fatal for a program.
    // The problem with this detection is that it does not
    // detect if a thread releases multiple times and there
    // are enough waiters in queue to cover the additional
    // releases
    assert(current <= atomic_load(&spinlock->Next));
}

void
SpinlockReleaseIrq(
        _In_ Spinlock_t* spinlock)
{
    irqstate_t irqState;
    assert(spinlock != NULL);

    // Load irqstate before we unlock, because the moment
    // we lose the lock, another thread can grap it and override the
    // stored irq-state.
    irqState = spinlock->IrqState;

    // Re-use the non-irq code
    SpinlockRelease(spinlock);

    // Restore the original interrupt state
    InterruptRestoreState(irqState);
}
