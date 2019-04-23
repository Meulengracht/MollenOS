/* MollenOS
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
 * Mutex Support Definitions & Structures
 * - This header describes the base mutex-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <os/mutex.h>

int
MutexInitialize(
    _In_ Mutex_t* Mutex,
    _In_ int      Type)
{
    if (Mutex == NULL) {
        return MutexError;
    }
    
    Mutex->_flags = Type;
    Mutex->_owner = UUID_INVALID;
    Mutex->_count = ATOMIC_VAR_INIT(0);
    SpinlockReset(&Mutex->_syncobject, 0);
    return MutexSuccess;

}

int
MutexAcquire(
    _In_ Mutex_t* Mutex)
{
    if (Mutex == NULL) {
        return MutexError;
    }

    // If this thread already holds the mutex,
    // increase ref count, but only if we're recursive 
    if (Mutex->_flags & MutexRecursive) {
        while (1) {
            int initialcount = atomic_load(&Mutex->_count);
            if (initialcount != 0 && Mutex->_owner == thrd_current()) {
                if (atomic_compare_exchange_weak(&Mutex->_count, &initialcount, initialcount + 1)) {
                    return MutexSuccess;
                }
                continue;
            }
            break;
        }
    }

    // acquire lock and set information
    SpinlockAcquire(&Mutex->_syncobject);
    Mutex->_owner = thrd_current();
    atomic_store(&Mutex->_count, 1);
    return MutexSuccess;
}

int
MutexTryAcquire(
    _In_ Mutex_t* Mutex)
{
    if (Mutex == NULL) {
        return MutexError;
    }

    // If this thread already holds the mutex,
    // increase ref count, but only if we're recursive 
    if (Mutex->_flags & MutexRecursive) {
        while (1) {
            int initialcount = atomic_load(&Mutex->_count);
            if (initialcount != 0 && Mutex->_owner == thrd_current()) {
                if (atomic_compare_exchange_weak(&Mutex->_count, &initialcount, initialcount + 1)) {
                    return MutexSuccess;
                }
                continue;
            }
            break;
        }
    }
    
    // Go for an acquire attempt
    if (SpinlockTryAcquire(&Mutex->_syncobject) == OsError) {
        return MutexBusy;
    }
    Mutex->_owner = thrd_current();
    atomic_store(&Mutex->_count, 1);
    return MutexSuccess;
}

int
MutexRelease(
    _In_ Mutex_t* Mutex)
{
    int initialcount;
    if (Mutex == NULL) {
        return MutexError;
    }

    // Sanitize state of the mutex, are we even able to unlock it?
    initialcount = atomic_load(&Mutex->_count);
    if (initialcount == 0 || Mutex->_owner != thrd_current()) {
        return MutexError;
    }
    
    initialcount = atomic_fetch_sub(&Mutex->_count, 1) - 1;
    if (initialcount == 0) {
        Mutex->_owner = UUID_INVALID;
        SpinlockRelease(&Mutex->_syncobject);
    }
    return MutexSuccess;
}

void
MutexDestroy(
    _In_ Mutex_t* Mutex)
{
    _CRT_UNUSED(Mutex);
}
