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
#include <ddk/utils.h>
#include <string.h>

struct __LocalHandle {
    struct OSHandle OSHandle;
    // References refers the number of local references an OS handle
    // might have. As the structure name indicates, no other process
    // has any idea of the references we keep here. It's primarily to
    // support fileviews and other similar concepts, which are mostly
    // local to the process.
    int References;
    // Closing excludes new aliases after a possibly partial final release.
    bool Closing;
    // Releasing excludes simultaneous cleanup while the registry is unlocked.
    bool Releasing;
};

struct __HandleLookup {
    uuid_t               ID;
    struct __LocalHandle Match;
    unsigned int         Matches;
};

#define OSHANDLE_FLAG_OWNERSHIP 0x0001

static uint64_t __HandleHash(const void* element);
static int      __HandleCompare(const void* element1, const void* element2);

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
        sizeof(struct __LocalHandle),
        __HandleHash,
        __HandleCompare
    );
    spinlock_init(&g_osHandlesLock);
}

static void
__InsertHandle(
    _In_ struct OSHandle* handle)
{
    void* result;

    spinlock_acquire(&g_osHandlesLock);
    result = hashtable_set(
            &g_osHandles,
            &(struct __LocalHandle) {
                    .OSHandle = {
                            .ID = handle->ID,
                            .Type = handle->Type,
                            .Flags = handle->Flags,
                            .Payload = handle->Payload
                    },
                    .References = 1
            }
    );
    spinlock_release(&g_osHandlesLock);
    assert(result == NULL);
}

oserr_t
OSHandleCreate(
    _In_  OSHandle_t* registration,
    _In_  uint32_t    accessMask,
    _Out_ OSHandle_t* resourceOut)
{
    uuid_t  id;
    oserr_t status;

    if (resourceOut == NULL) {
        return OS_EINVALPARAMS;
    }
    
    status = Syscall_HandleCreate(
        registration != NULL ? registration->ID : UUID_INVALID,
        accessMask,
        &id
    );
    if (status != OS_EOK) {
        return status;
    }
    return OSHandleWrap(id, OSHANDLE_NULL, NULL, true, resourceOut);
}

oserr_t
OSHandleWrap(
    _In_ uuid_t            id,
    _In_ enum OSHandleType type,
    _In_ void*             payload,
    _In_ bool              ownership,
    _In_ struct OSHandle*  handle)
{
    handle->ID = id;
    handle->Type = type;
    handle->Flags = ownership ? OSHANDLE_FLAG_OWNERSHIP : 0;
    handle->Payload = payload;
    __InsertHandle(handle);
    return OS_EOK;
}

oserr_t
OSHandleRelease(
    _InOut_ struct OSHandle* handle)
{
    const struct OSHandleOps* handlers;
    struct __LocalHandle     key = { 0 };
    struct __LocalHandle*    entry;
    struct OSHandle          owned;
    oserr_t                  status = OS_EOK;
    bool                     finalize = false;

    if (handle == NULL || handle->ID == UUID_INVALID) {
        return OS_EOK;
    }

    if (handle->Type >= __OSHANDLE_COUNT) {
        return OS_EINVALPARAMS;
    }

    key.OSHandle = *handle;
    
    spinlock_acquire(&g_osHandlesLock);
    entry = hashtable_get(&g_osHandles, &key);
    if (entry == NULL) {
        status = OS_ENOENT;
        goto unlock;
    }
    
    if (entry->OSHandle.Type != handle->Type || entry->OSHandle.Payload != handle->Payload) {
        status = OS_EINVALPARAMS;
        goto unlock;
    }
    
    if (entry->Releasing) {
        status = OS_EBUSY;
        goto unlock;
    }
    
    if (entry->References > 1) {
        // A local alias shares its attachment. Dropping that alias must not
        // unmap the CPU view or make a syscall while another local owner exists.
        entry->References--;
        memset(handle, 0, sizeof(*handle));
        goto unlock;
    }

    entry->Closing = true;
    entry->Releasing = true;
    owned = entry->OSHandle;
    spinlock_release(&g_osHandlesLock);

    // Cleanup may block, yield or insert other handles. Copy the descriptor,
    // and look up the entry again afterwards: table growth can relocate it.
    // Keeping the entry reserved makes reentrant release/acquire fail safely.
    handlers = g_osHandlers[owned.Type];
    if (handlers->Release != NULL) {
        status = handlers->Release(&owned);
    } else if (handlers->Destroy != NULL) {
        handlers->Destroy(&owned);
    } else if (owned.Flags & OSHANDLE_FLAG_OWNERSHIP) {
        status = Syscall_HandleDestroy(owned.ID);
        // The kernel dropped our reference even when other references remain.
        if (status == OS_EINCOMPLETE) {
            status = OS_EOK;
        }
    }

    spinlock_acquire(&g_osHandlesLock);
    entry = hashtable_get(&g_osHandles, &key);
    if (status == OS_EOK) {
        hashtable_remove(&g_osHandles, &key);
        memset(handle, 0, sizeof(*handle));
        finalize = handlers->Release != NULL && handlers->Destroy != NULL;
    } else {
        // The payload holds any completed cleanup stages. Keep the last local
        // reference and disallow new users, but allow this owner to retry.
        entry->Releasing = false;
    }

unlock:
    spinlock_release(&g_osHandlesLock);
    if (finalize) {
        // The payload is also the attachment key. Keep it allocated until the
        // old entry is gone, so allocator reuse cannot replace a reserved key.
        handlers->Destroy(&owned);
    }
    return status;
}

void
OSHandleDestroy(
    _In_ struct OSHandle* handle)
{
    oserr_t status;

    // Preserve the legacy entry point without throwing away failed ownership.
    // Callers with retryable lifetimes must consume OSHandleRelease's status.
    status = OSHandleRelease(handle);
    if (status != OS_EOK) {
        (void)Syscall_Debug(OSSYSLOGLEVEL_WARNING,
            "OSHandleDestroy: release failed; owner retained, retry with OSHandleRelease");
    }
}

oserr_t
OSHandlesAcquire(
    _In_ struct OSHandle* handle)
{
    oserr_t               oserr = OS_ENOENT;
    struct __LocalHandle* entry;

    spinlock_acquire(&g_osHandlesLock);
    entry = hashtable_get(
            &g_osHandles,
            &(struct __LocalHandle) {
                    .OSHandle = *handle
            }
    );
    if (entry != NULL && entry->Closing) {
        oserr = OS_EBUSY;
    } else if (entry != NULL) {
        entry->References++;
        oserr = OS_EOK;
    }
    spinlock_release(&g_osHandlesLock);
    return oserr;
}

static void
__FindAttachment(
    _In_ int         index,
    _In_ const void* element,
    _In_ void*       context)
{
    const struct __LocalHandle* entry = element;
    struct __HandleLookup*      lookup = context;

    _CRT_UNUSED(index);
    if (entry->OSHandle.ID == lookup->ID) {
        lookup->Match = *entry;
        lookup->Matches++;
    }
}

oserr_t
OSHandlesFind(
    _In_  uuid_t           id,
    _Out_ struct OSHandle* handle)
{
    oserr_t               status = OS_ENOENT;
    struct __LocalHandle* entry;
    struct __HandleLookup lookup = { .ID = id };

    spinlock_acquire(&g_osHandlesLock);
    entry = hashtable_get(
        &g_osHandles,
        &(struct __LocalHandle) {
            .OSHandle.ID = id
        }
    );
    if (entry == NULL) {
        // Ordinary handles keep their ID-only index. Independent SHM views
        // need a payload key, so the legacy ID-only query may be ambiguous.
        // Attachment acquire/release use the full key and never take this scan.
        hashtable_enumerate(&g_osHandles, __FindAttachment, &lookup);
        if (lookup.Matches > 1) {
            status = OS_EEXISTS;
            goto unlock;
        }
        if (lookup.Matches == 1) {
            entry = &lookup.Match;
        }
    }

    if (entry != NULL) {
        if (entry->Closing) {
            status = OS_EBUSY;
        } else {
            *handle = entry->OSHandle;
            status = OS_EOK;
        }
    }

unlock:
    spinlock_release(&g_osHandlesLock);
    return status;
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

static uint64_t
__HandleHash(
    _In_ const void* element)
{
    const struct __LocalHandle* handle = element;
    uint64_t                    hash = handle->OSHandle.ID;

    // SHMAttach creates an independent view and kernel reference for each
    // payload, even when the storage ID is shared. Aliases keep that payload.
    if (handle->OSHandle.Type == OSHANDLE_SHM) {
        hash ^= ((uint64_t)(uintptr_t)handle->OSHandle.Payload >> 3) * UINT64_C(0x9e3779b97f4a7c15);
    }
    return hash;
}

static int
__HandleCompare(
    _In_ const void* element1,
    _In_ const void* element2)
{
    const struct __LocalHandle* handle1 = element1;
    const struct __LocalHandle* handle2 = element2;

    if (handle1->OSHandle.ID != handle2->OSHandle.ID) {
        return -1;
    }
    if (handle1->OSHandle.Type == OSHANDLE_SHM || handle2->OSHandle.Type == OSHANDLE_SHM) {
        return handle1->OSHandle.Type == handle2->OSHandle.Type &&
               handle1->OSHandle.Payload == handle2->OSHandle.Payload ? 0 : -1;
    }
    return 0;
}
