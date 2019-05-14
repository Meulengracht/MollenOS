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

extern int _spinlock_acquire(spinlock_t* lock);
extern int _spinlock_test(spinlock_t* lock);
extern void _spinlock_release(spinlock_t* lock);

#define IS_RECURSIVE(lock) (lock->_type & spinlock_recursive)

void 
spinlock_init(
	_In_ spinlock_t* lock,
    _In_ int         type)
{
    assert(lock != NULL);

	lock->_val   = 0;
    lock->_owner = UUID_INVALID;
    lock->_type  = type;
    atomic_store(&lock->_refs, 0);
}

int
spinlock_try_acquire(
	_In_ spinlock_t* lock)
{
    assert(lock != NULL);

    if (IS_RECURSIVE(lock) && lock->_owner == thrd_current()) {
        int References = atomic_fetch_add(&lock->_refs, 1);
        assert(References != 0);
        return spinlock_acquired;
    }

    // Value is updated by _acquire
	if (!_spinlock_test(lock)) {
        return spinlock_busy;
    }
    
    atomic_store(&lock->_refs, 1);
    lock->_owner = thrd_current();
    return spinlock_acquired;
}

void
spinlock_acquire(
	_In_ spinlock_t* lock)
{
    while (spinlock_try_acquire(lock) != spinlock_acquired);
}

int
spinlock_release(
	_In_ spinlock_t* lock)
{
    int References;
    assert(lock != NULL);

    // Reduce the number of references
    References = atomic_fetch_sub(&lock->_refs, 1);
    if (References == 1) {
        lock->_owner = UUID_INVALID;
        _spinlock_release(lock);
        return spinlock_released;
    }
    return spinlock_acquired;
}
