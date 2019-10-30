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

#include <assert.h>
#include <irq_spinlock.h>

void
IrqSpinlockConstruct(
    _In_ IrqSpinlock_t* Spinlock)
{
    assert(Spinlock != NULL);
    
    spinlock_init(&Spinlock->SyncObject, spinlock_plain);
    Spinlock->Flags = 0;
}

void
IrqSpinlockAcquire(
    _In_ IrqSpinlock_t* Spinlock)
{
    IntStatus_t Flags;
    assert(Spinlock != NULL);
    
    Flags = InterruptDisable();
    spinlock_acquire(&Spinlock->SyncObject);
    Spinlock->Flags = Flags;
}

void
IrqSpinlockRelease(
    _In_ IrqSpinlock_t* Spinlock)
{
    IntStatus_t Flags;
    assert(Spinlock != NULL);
    
    Flags = Spinlock->Flags;
    spinlock_release(&Spinlock->SyncObject);
    InterruptRestoreState(Flags);
}
