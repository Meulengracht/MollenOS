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

#include <assert.h>
#include <ddk/barrier.h>
#include <internal/_syscalls.h>
#include <os/spinlock.h>
#include <threads.h>

extern int  _spinlock_acquire(spinlock_t* lock);
extern int  _spinlock_test(spinlock_t* lock);
extern void _spinlock_release(spinlock_t* lock);

#define IS_RECURSIVE(lock) (lock->type & spinlock_recursive)

void 
spinlock_init(
	_In_ spinlock_t* lock,
    _In_ int         type)
{
    assert(lock != NULL);

	lock->value      = 0;
    lock->owner      = UUID_INVALID;
    lock->type       = type;
    lock->references = ATOMIC_VAR_INIT(0);
    smp_wmb();
}

int
spinlock_try_acquire(
	_In_ spinlock_t* lock)
{
    int    references;
    thrd_t currentThread = thrd_current();
    
    assert(lock != NULL);

    if (IS_RECURSIVE(lock) && lock->owner == currentThread) {
        references = atomic_fetch_add(&lock->references, 1);
        assert(references != 0);
        return spinlock_acquired;
    }

	if (!_spinlock_test(lock)) {
        return spinlock_busy;
    }
    
    lock->owner = thrd_current();
    atomic_store(&lock->references, 1);
    return spinlock_acquired;
}

void
spinlock_acquire(
	_In_ spinlock_t* lock)
{
    int references;
    
    assert(lock != NULL);

    if (IS_RECURSIVE(lock) && lock->owner == thrd_current()) {
        references = atomic_fetch_add(&lock->references, 1);
        assert(references != 0);
        return;
    }
    
    _spinlock_acquire(lock);
    lock->owner = thrd_current();
    atomic_store(&lock->references, 1);
}

int
spinlock_release(
	_In_ spinlock_t* lock)
{
    int references;
    assert(lock != NULL);

    references = atomic_fetch_sub(&lock->references, 1);
    if (references == 1) {
        lock->owner = UUID_INVALID;
        _spinlock_release(lock);
        return spinlock_released;
    }
    return spinlock_acquired;
}
