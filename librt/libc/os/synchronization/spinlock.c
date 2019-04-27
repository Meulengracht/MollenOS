/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * Spinlock Support Definitions & Structures
 * - This header describes the base spinlock-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <internal/_syscalls.h>
#include <os/spinlock.h>
#include <threads.h>
#include <assert.h>

extern int _spinlock_acquire(Spinlock_t *Spinlock);
extern int _spinlock_test(Spinlock_t *Spinlock);
extern void _spinlock_release(Spinlock_t *Spinlock);

#define IS_RECURSIVE(Spinlock) (Spinlock->Configuration & SPINLOCK_RECURSIVE)

OsStatus_t 
SpinlockReset(
	_In_ Spinlock_t *Lock,
    _In_ Flags_t     Configuration)
{
    assert(Lock != NULL);
    atomic_store(&Lock->References, 0);

	Lock->Value         = 0;
    Lock->Owner         = UUID_INVALID;
    Lock->Configuration = Configuration;
	return OsSuccess;
}

OsStatus_t
SpinlockTryAcquire(
	_In_ Spinlock_t *Lock)
{
    assert(Lock != NULL);

    if (IS_RECURSIVE(Lock) && Lock->Owner == thrd_current()) {
        atomic_fetch_add(&Lock->References, 1);
        return OsSuccess;
    }

    // Value is updated by _acquire
	if (!_spinlock_test(Lock)) {
        return OsError;
    }
    
    atomic_store(&Lock->References, 1);
    Lock->Owner = thrd_current();
    return OsSuccess;
}

void
SpinlockAcquire(
	_In_ Spinlock_t *Lock)
{
    while (SpinlockTryAcquire(Lock) != OsSuccess);
}

void 
SpinlockRelease(
	_In_ Spinlock_t *Lock)
{
    int References;
    assert(Lock != NULL);

    // Reduce the number of references
    References = atomic_fetch_sub(&Lock->References, 1);
    if (References == 1) {
        Lock->Owner = UUID_INVALID;
        _spinlock_release(Lock);
    }
}
