/**
 * Copyright 2023, Philip Meulengracht
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
 */

#include <assert.h>
#include <ds/hashtable.h>
#include <internal/_syscalls.h>
#include <os/handle.h>
#include <os/spinlock.h>
#include <string.h>

#define OSHANDLE_FLAG_OWNERSHIP 0x0001

static uint64_t handle_hash(const void* element);
static int      handle_cmp(const void* element1, const void* element2);

static const OSHandleOps_t g_nullOps = {};
extern const OSHandleOps_t g_osFileOps;
extern const OSHandleOps_t g_eventOps;
extern const OSHandleOps_t g_hqueueOps;
extern const OSHandleOps_t g_ipcOps;
extern const OSHandleOps_t g_shmOps;
extern const OSHandleOps_t g_socketOps;

static const OSHandleOps_t* g_osHandlers[__OSHANDLE_COUNT] = {
        &g_nullOps,
        &g_osFileOps,
        &g_eventOps,
        &g_hqueueOps,
        &g_shmOps,
        &g_socketOps
};
static hashtable_t g_osHandles;
static spinlock_t  g_osHandlesLock;

void OSHandlesInitialize(void)
{
    hashtable_construct(
            &g_osHandles,
            0,
            sizeof(struct OSHandle),
            handle_hash,
            handle_cmp
    );
    spinlock_init(&g_osHandlesLock);
}

oserr_t
OSHandleCreate(
        _In_ enum OSHandleType type,
        _In_ void*             payload,
        _In_ struct OSHandle*  handle)
{
    oserr_t oserr;
    void*   result;

    if (handle == NULL) {
        return OS_EINVALPARAMS;
    }

    oserr = Syscall_CreateHandle(&handle->ID);
    if (oserr != OS_EOK) {
        return oserr;
    }

    handle->Type = type;
    handle->Flags = OSHANDLE_FLAG_OWNERSHIP;
    handle->Payload = payload;

    spinlock_acquire(&g_osHandlesLock);
    result = hashtable_set(
            &g_osHandles,
            handle
    );
    assert(result == NULL);
    spinlock_release(&g_osHandlesLock);
    return OS_EOK;
}

oserr_t
OSHandleWrap(
        _In_ uuid_t            id,
        _In_ enum OSHandleType type,
        _In_ void*             payload,
        _In_ bool              ownership,
        _In_ struct OSHandle*  handle)
{
    void* result;

    handle->ID = id;
    handle->Type = type;
    handle->Flags = ownership ? OSHANDLE_FLAG_OWNERSHIP : 0;
    handle->Payload = payload;

    spinlock_acquire(&g_osHandlesLock);
    result = hashtable_set(
            &g_osHandles,
            handle
    );
    assert(result == NULL);
    spinlock_release(&g_osHandlesLock);
    return OS_EOK;
}

void
OSHandleDestroy(
        _In_ struct OSHandle* handle)
{
    const struct OSHandleOps* handlers;
    struct OSHandle*          entry = NULL;

    spinlock_acquire(&g_osHandlesLock);
    entry = hashtable_remove(
            &g_osHandles,
            handle
    );
    spinlock_release(&g_osHandlesLock);
    if (entry == NULL) {
        return;
    }

    // Let the custom destructor handle it if it's set.
    handlers = g_osHandlers[handle->Type];
    if (handlers->Destroy) {
        handlers->Destroy(handle);
        return;
    }

    // Otherwise we do the default destructor
    if (handle->Flags & OSHANDLE_FLAG_OWNERSHIP) {
        (void)Syscall_DestroyHandle(handle->ID);
    }
}

oserr_t
OSHandlesFind(
        _In_ uuid_t           id,
        _In_ struct OSHandle* handle)
{
    oserr_t oserr = OS_ENOENT;
    void*   entry;

    spinlock_acquire(&g_osHandlesLock);
    entry = hashtable_get(
            &g_osHandles,
            &(struct OSHandle) {
                    .ID = id
            }
    );
    if (entry != NULL) {
        memcpy(handle, entry, sizeof(struct OSHandle));
        oserr = OS_EOK;
    }
    hashtable_set(&g_osHandles, handle);
    spinlock_release(&g_osHandlesLock);
    return oserr;
}

static void
__SerializeHandle(
        _In_ struct OSHandle* handle,
        _In_ uint8_t*         buffer)
{
    *((uuid_t*)buffer) = handle->ID;
    *((uint16_t*)&buffer[sizeof(uuid_t)]) = handle->Type;
    *((uint16_t*)&buffer[sizeof(uuid_t) + sizeof(uint16_t)]) = handle->Flags;
}

size_t
OSHandleSerialize(
        _In_ struct OSHandle* handle,
        _In_ void*            buffer)
{
    const struct OSHandleOps* handlers;
    uint8_t*                  buffer8 = buffer;

    if (handle == NULL || handle->Type >= __OSHANDLE_COUNT) {
        return 0;
    }

    __SerializeHandle(handle, buffer);
    handlers = g_osHandlers[handle->Type];
    if (handlers->Serialize) {
        return __HEADER_SIZE_RAW + handlers->Serialize(handle, &buffer8[__HEADER_SIZE_RAW]);
    }
    return __HEADER_SIZE_RAW;
}

static void
__DeserializeHandle(
        _In_ struct OSHandle* handle,
        _In_ const uint8_t*   buffer)
{
    handle->ID = *((uuid_t*)&buffer[0]);
    handle->Type = *((uint16_t*)&buffer[sizeof(uuid_t)]);
    handle->Flags = *((uint16_t*)&buffer[sizeof(uuid_t) + sizeof(uint16_t)]);
    handle->Payload = NULL;
}

size_t
OSHandleDeserialize(
        _In_ struct OSHandle* handle,
        _In_ const void*      buffer)
{
    const struct OSHandleOps* handlers;
    const uint8_t*            buffer8 = buffer;

    if (buffer == NULL) {
        return 0;
    }

    __DeserializeHandle(handle, buffer8);
    handlers = g_osHandlers[handle->Type];
    if (handlers->Deserialize) {
        return __HEADER_SIZE_RAW + handlers->Deserialize(handle, &buffer8[__HEADER_SIZE_RAW]);
    }
    return __HEADER_SIZE_RAW;
}

static uint64_t handle_hash(const void* element)
{
    const struct OSHandle* handle = element;
    return handle->ID;
}

static int handle_cmp(const void* element1, const void* element2)
{
    const struct OSHandle* handle1 = element1;
    const struct OSHandle* handle2 = element2;
    return handle1->ID == handle2->ID ? 0 : -1;
}
