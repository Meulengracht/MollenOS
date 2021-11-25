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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Synchronization (Irq Spinlocks)
 * - Spinlock implementation that also disables interrupts.
 */

#include <arch/interrupts.h>
#include <assert.h>
#include <irq_spinlock.h>
#include <threading.h>

extern int  _spinlock_acquire(IrqSpinlock_t* lock);
extern int  _spinlock_test(IrqSpinlock_t* lock);
extern void _spinlock_release(IrqSpinlock_t* lock);

void
IrqSpinlockConstruct(
    _In_ IrqSpinlock_t* spinlock)
{
    assert(spinlock != NULL);

    spinlock->value = 0;
    spinlock->owner = 0xFFFFFFFF;
    spinlock->original_flags = 0;
    spinlock->references = 0;
}

void
IrqSpinlockAcquire(
    _In_ IrqSpinlock_t* spinlock)
{
    IntStatus_t flags;
    assert(spinlock != NULL);

    if (spinlock->owner == ThreadCurrentHandle()) {
        spinlock->references++;
        return;
    }

    flags = InterruptDisable();
    _spinlock_acquire(spinlock);
    spinlock->original_flags = flags;
    spinlock->references = 1;
    spinlock->owner = ThreadCurrentHandle();
}

void
IrqSpinlockRelease(
    _In_ IrqSpinlock_t* spinlock)
{
    IntStatus_t flags;

    assert(spinlock != NULL);
    assert(spinlock->owner == ThreadCurrentHandle());

    flags = spinlock->original_flags;
    spinlock->references--;
    if (!spinlock->references) {
        spinlock->owner = 0;
        _spinlock_release(spinlock);
        InterruptRestoreState(flags);
    }
}
