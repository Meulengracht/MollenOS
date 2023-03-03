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

static struct OSHandler {
    bool                  Valid;
    OSHandleDestroyFn     DctorFn;
    OSHandleSerializeFn   ExportFn;
    OSHandleDeserializeFn ImportFn;
} g_osHandlers[__OSHANDLE_COUNT] = {
        { NULL, NULL }
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
OSHandlesRegisterHandlers(
        _In_ uint32_t              type,
        _In_ OSHandleDestroyFn     dctorFn,
        _In_ OSHandleSerializeFn   exportFn,
        _In_ OSHandleDeserializeFn importFn)
{
    if (g_osHandlers[type].Valid) {
        return OS_EEXISTS;
    }
    g_osHandlers[type].Valid = true;
    g_osHandlers[type].DctorFn = dctorFn;
    g_osHandlers[type].ExportFn = exportFn;
    g_osHandlers[type].ImportFn = importFn;
    return OS_EOK;
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
        _In_ uuid_t id)
{
    struct OSHandle* handle;
    uint16_t         flags = 0;

    spinlock_acquire(&g_osHandlesLock);
    handle = hashtable_remove(
            &g_osHandles,
            &(struct OSHandle) {
                .ID = id
            }
    );
    if (handle != NULL) {
        flags = handle->Flags;
    }
    spinlock_release(&g_osHandlesLock);

    // Finally destroy the handle
    if (flags & OSHANDLE_FLAG_OWNERSHIP) {
        (void)Syscall_DestroyHandle(id);
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

oserr_t
OSHandleSerialize(
        _In_ struct OSHandle* handle,
        _In_ void*            buffer)
{
    struct OSHandler* handlers;
    uint8_t*          buffer8 = buffer;

    if (handle == NULL) {
        return OS_EINVALPARAMS;
    }
    if (handle->Type >= __OSHANDLE_COUNT) {
        return OS_ENOTSUPPORTED;
    }

    handlers = &g_osHandlers[handle->ID];
    if (handlers->ExportFn) {
        return handlers->ExportFn(handle, &buffer8[__HEADER_SIZE_RAW]);
    }
    return OS_EOK;
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

oserr_t
OSHandleDeserialize(
        _In_ struct OSHandle* handle,
        _In_ const void*      buffer)
{
    struct OSHandler* handlers;
    const uint8_t*    buffer8 = buffer;

    if (buffer == NULL) {
        return OS_EINVALPARAMS;
    }

    __DeserializeHandle(handle, buffer8);
    handlers = &g_osHandlers[handle->ID];
    if (handlers->ImportFn) {
        return handlers->ImportFn(handle, &buffer8[__HEADER_SIZE_RAW]);
    }
    return OS_EOK;
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
