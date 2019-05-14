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
 * Mutex Support Definitions & Structures
 * - This header describes the base mutex-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <internal/_syscalls.h>
#include <os/spinlock.h>
#include <os/mollenos.h>
#include <threads.h>
#include <time.h>

#define MUTEX_SPINS 1000

static SystemDescriptor_t SystemInfo = { 0 };

int
mtx_init(
    _In_ mtx_t* mutex,
    _In_ int    type)
{
    if (mutex == NULL) {
        return thrd_error;
    }

    // Get information about the system
    if (SystemInfo.NumberOfActiveCores == 0) {
        SystemQuery(&SystemInfo);
    }
    
    // Initialize the two spinlocks, the _lock must be initialized
    // as requested, however the syncobject must be plain (non-recursive)
    spinlock_init(&mutex->_lock, type);
    spinlock_init(&mutex->_syncobject, spinlock_plain);
    cnd_init(&mutex->_condition);
    mutex->_flags = type;
    return thrd_success;
}

void
mtx_destroy(
    _In_ mtx_t* mutex)
{
    if (mutex != NULL) {
        spinlock_init(&mutex->_lock, mutex->_flags);
        spinlock_init(&mutex->_syncobject, mutex->_flags);
        cnd_destroy(&mutex->_condition);
    }
}

int
mtx_trylock(
    _In_ mtx_t* mutex)
{
    if (mutex == NULL) {
        return thrd_error;
    }
    return spinlock_try_acquire(&mutex->_lock);
}

int
mtx_lock(
    _In_ mtx_t* mutex)
{
    int i;
    
    if (mutex == NULL) {
        return thrd_error;
    }
    
    // In a multcore environment the lock may not be held for long, so
    // perform X iterations before going for a longer block
    // period.
    if (SystemInfo.NumberOfActiveCores > 1) {
        for (i = 0; i < MUTEX_SPINS; i++) {
            if (spinlock_try_acquire(&mutex->_lock) == spinlock_acquired) {
                return thrd_success;
            }
        }
    }
    
    spinlock_acquire(&mutex->_syncobject);
    while (1) {
        if (spinlock_try_acquire(&mutex->_lock) == spinlock_acquired) {
            break;
        }
        Syscall_WaitQueueBlock(mutex->_condition, &mutex->_syncobject, 0);
    }
    spinlock_release(&mutex->_syncobject);
    return thrd_success;
}

int
mtx_timedlock(
    _In_ mtx_t* restrict                 mutex,
    _In_ const struct timespec* restrict time_point)
{
    time_t          msec   = 0;
	struct timespec now, result;
	int             success;
    
    if (mutex == NULL || !(mutex->_flags & mtx_timed)) {
        return thrd_error;
    }
    
    // Calculate time to sleep
	timespec_get(&now, TIME_UTC);
    timespec_diff(time_point, &now, &result);
    msec = result.tv_sec * MSEC_PER_SEC;
    if (result.tv_nsec != 0) {
        msec += ((result.tv_nsec - 1) / NSEC_PER_MSEC) + 1;
    }
    
	spinlock_acquire(&mutex->_syncobject);
    while (1) {
        success = spinlock_try_acquire(&mutex->_lock);
        if (success == spinlock_acquired) {
            success = thrd_success;
            break;
        }
        
        // Only handle the timeout seperately, otherwise just test again
        if (Syscall_WaitQueueBlock(mutex->_condition, &mutex->_syncobject, msec) == OsTimeout) {
            success = thrd_timedout;
            break;
        }
    }
    spinlock_release(&mutex->_syncobject);
    return success;
}

int
mtx_unlock(
    _In_ mtx_t* mutex)
{
    if (mutex == NULL) {
        return thrd_error;
    }

    spinlock_acquire(&mutex->_syncobject);
    if (spinlock_release(&mutex->_lock) == spinlock_released) {
        Syscall_WaitQueueUnblock(&mutex->_condition);
    }
    spinlock_release(&mutex->_syncobject);
    return thrd_success;
}
