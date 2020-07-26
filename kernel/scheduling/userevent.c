/**
 * MollenOS
 *
 * Copyright 2020, Philip Meulengracht
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
 * Synchronization (UserEvent)
 * - Userspace events that can emulate some of the linux event descriptors like eventfd, timerfd etc. They also
 *   provide binary semaphore functionality that can provide light synchronization primitives for interrupts
 */

#include <futex.h>
#include <handle.h>
#include <handle_set.h>
#include <heap.h>
#include <userevent.h>
#include <ioset.h>

typedef struct UserEvent {
    size_t       initalValue;
    unsigned int flags;
    atomic_int*  sync_address;
} UserEvent_t;

static MemoryCache_t* syncAddressCache = NULL;

void
UserEventInitialize(void)
{
    syncAddressCache = MemoryCacheCreate(
            "userevent_cache",
            sizeof(atomic_int),
            sizeof(void*),
            0,
            HEAP_SLAB_NO_ATOMIC_CACHE | HEAP_CACHE_USERSPACE,
            NULL,
            NULL);
}

static void
UserEventDestroy(
    _In_ void* resource)
{
    UserEvent_t* event = resource;
    if (!event) {
        return;
    }

    MemoryCacheFree(syncAddressCache, event->sync_address);
    kfree(event);
}

OsStatus_t
UserEventCreate(
    _In_  size_t       initialValue,
    _In_  unsigned int flags,
    _Out_ UUId_t*      handleOut,
    _Out_ atomic_int** syncAddressOut)
{
    UserEvent_t* event;
    UUId_t       handle;

    if (!handleOut || !syncAddressOut) {
        return OsInvalidParameters;
    }

    event = kmalloc(sizeof(UserEvent_t));
    if (!event) {
        return OsOutOfMemory;
    }

    event->initalValue  = initialValue;
    event->flags        = flags;
    event->sync_address = (atomic_int*)MemoryCacheAllocate(syncAddressCache);
    if (!event->sync_address) {
        kfree(event);
        return OsOutOfMemory;
    }

    handle = CreateHandle(HandleTypeUserEvent, UserEventDestroy, event);
    if (handle == UUID_INVALID) {
        MemoryCacheFree(syncAddressCache, event->sync_address);
        kfree(event);
        return OsError;
    }

    if (EVT_TYPE(flags) == EVT_TIMEOUT_EVENT) {
        // @todo initate the timeout event
    }

    *handleOut = handle;
    *syncAddressOut = event->sync_address;
    return OsSuccess;
}

OsStatus_t
UserEventSignal(
    _In_ UUId_t handle)
{
    UserEvent_t* event  = LookupHandleOfType(handle, HandleTypeUserEvent);
    OsStatus_t   status = OsIncomplete;
    int          currentValue;
    int          i;
    int          result;
    int          value = 1;

    if (!event) {
        return OsInvalidParameters;
    }

    // assert not max
    currentValue = atomic_load(event->sync_address);
    if ((currentValue + value) <= event->initalValue) {
        for (i = 0; i < value; i++) {
            while ((currentValue + 1) <= event->initalValue) {
                result = atomic_compare_exchange_weak(event->sync_address,
                        &currentValue, currentValue + 1);
                if (result) {
                    break;
                }
                FutexWake(event->sync_address, 1, 0);
            }
        }
    }

    MarkHandle(handle, EVT_TYPE(event->flags) == EVT_TIMEOUT_EVENT ? IOSETTIM : IOSETSYN);
    return status;
}
