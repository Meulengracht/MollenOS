/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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

#define IS_RECURSIVE(lock) (lock->type & spinlock_recursive)

void 
spinlock_init(
	_In_ spinlock_t* lock,
    _In_ int         type)
{
    assert(lock != NULL);

	lock->value = 0;
    lock->owner = UUID_INVALID;
    lock->type  = type;
    atomic_store(&lock->references, 0);
}

int
spinlock_try_acquire(
	_In_ spinlock_t* lock)
{
    int references;
    
    assert(lock != NULL);

    BARRIER_FULL;
    if (IS_RECURSIVE(lock) && lock->owner == thrd_current()) {
        references = atomic_fetch_add(&lock->references, 1);
        BARRIER_FULL;
        assert(references != 0);
        return spinlock_acquired;
    }

    // Value is updated by _acquire
	if (!_spinlock_test(lock)) {
        return spinlock_busy;
    }
    
    atomic_store(&lock->references, 1);
    lock->owner = thrd_current();
    BARRIER_STORE;
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
    int references;
    assert(lock != NULL);

    BARRIER_LOAD;
    references = atomic_fetch_sub(&lock->references, 1);
    BARRIER_FULL;
    if (references == 1) {
        lock->owner = UUID_INVALID;
        _spinlock_release(lock);
        BARRIER_STORE;
        return spinlock_released;
    }
    return spinlock_acquired;
}
