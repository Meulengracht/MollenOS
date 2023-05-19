/**
 * Copyright 2018, Philip Meulengracht
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
 *ock
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Resource Handle Interface
 * - Implementation of the resource handle interface. This provides system-wide
 *   resource handles and maintience of resources. 
 */

#define __MODULE "handle"
//#define __TRACE

#include <assert.h>
#include <ds/hashtable.h>
#include <ds/mstring.h>
#include <ds/queue.h>
#include <debug.h>
#include <handle.h>
#include <threading.h>

// Handle is being destroyed
#define __HANDLE_FLAG_DESTROYING 0x1

struct ResourceHandle {
    element_t          QueueHeader;
    uuid_t             ID;
    HandleType_t       Type;
    unsigned int       Flags;
    mstring_t*         Path;
    void*              Resource;
    int                References;
    HandleDestructorFn Destructor;
};

struct HandleMapping {
    mstring_t* path;
    uuid_t     handle;
};

_Noreturn static void HandleJanitorThread(void* arg);
static uint64_t mapping_hash(const void* element);
static int      mapping_cmp(const void* element1, const void* element2);
static uint64_t handle_hash(const void* element);
static int      handle_cmp(const void* element1, const void* element2);

static hashtable_t     g_handlemappings;
static hashtable_t     g_handles;
static Spinlock_t      g_handlesLock; // use irq lock as we use the handles from interrupts
static _Atomic(uuid_t) g_nextHandleId  = 0;
static Semaphore_t     g_eventHandle   = SEMAPHORE_INIT(0, 1);
static queue_t         g_cleanQueue    = QUEUE_INIT;
static uuid_t          g_janitorHandle = UUID_INVALID;

oserr_t
InitializeHandles(void)
{
    hashtable_construct(&g_handlemappings, HASHTABLE_MINIMUM_CAPACITY,
                        sizeof(struct HandleMapping), mapping_hash,
                        mapping_cmp);
    hashtable_construct(&g_handles, HASHTABLE_MINIMUM_CAPACITY,
                        sizeof(struct ResourceHandle), handle_hash,
                        handle_cmp);
    SpinlockConstruct(&g_handlesLock);
    atomic_store(&g_nextHandleId, 1);
    return OS_EOK;
}

oserr_t
InitializeHandleJanitor(void)
{
    // Create the thread with all defaults
    return ThreadCreate("janitor", HandleJanitorThread, NULL,
                        0, UUID_INVALID, 0, 0,
                        &g_janitorHandle);
}

static inline struct ResourceHandle*
__LookupSafe(
        _In_ uuid_t ID)
{
    struct ResourceHandle* handle;
    if (!atomic_load(&g_nextHandleId)) {
        return NULL;
    }

    handle = hashtable_get(&g_handles, &(struct ResourceHandle) { .ID = ID });
    if (handle == NULL || (handle->Flags & __HANDLE_FLAG_DESTROYING)) {
        return NULL;
    }
    return handle;
}

static struct ResourceHandle*
__AcquireHandle(
        _In_ uuid_t handleId)
{
    struct ResourceHandle* handle;
    if (!atomic_load(&g_nextHandleId)) {
        return NULL;
    }

    SpinlockAcquireIrq(&g_handlesLock);
    handle = hashtable_get(&g_handles, &(struct ResourceHandle) { .ID = handleId });
    if (handle == NULL || (handle->Flags & __HANDLE_FLAG_DESTROYING)) {
        SpinlockReleaseIrq(&g_handlesLock);
        return NULL;
    }

    handle->References++;
    SpinlockReleaseIrq(&g_handlesLock);
    return handle;
}

uuid_t
CreateHandle(
    _In_ HandleType_t       handleType,
    _In_ HandleDestructorFn destructor,
    _In_ void*              resource)
{
    struct ResourceHandle handle;
    void*                 existing;

    ELEMENT_INIT(&handle.QueueHeader, NULL, NULL);
    handle.ID         = atomic_fetch_add(&g_nextHandleId, 1);
    handle.Type       = handleType;
    handle.Path       = NULL;
    handle.Resource   = resource;
    handle.Destructor = destructor;
    handle.References = 1;
    handle.Flags      = 0;

    SpinlockAcquireIrq(&g_handlesLock);
    existing = hashtable_set(&g_handles, &handle);
    assert(existing == NULL);
    SpinlockReleaseIrq(&g_handlesLock);
    return handle.ID;
}

oserr_t
AcquireHandle(
        _In_  uuid_t handleId,
        _Out_ void** resourceOut)
{
    struct ResourceHandle* handle;

    handle = __AcquireHandle(handleId);
    if (!handle) {
        return OS_ENOENT;
    }
    
    if (resourceOut) {
        *resourceOut = handle->Resource;
    }
    return OS_EOK;
}

oserr_t
AcquireHandleOfType(
        _In_  uuid_t       handleId,
        _In_  HandleType_t handleType,
        _Out_ void**       resourceOut)
{
    struct ResourceHandle* handle;

    handle = __AcquireHandle(handleId);
    if (!handle) {
        return OS_ENOENT;
    }

    if (handle->Type != handleType) {
        ERROR("AcquireHandleOfType requested handle type %u, but handle was of type %u",
              handleType, handle->Type);
        DestroyHandle(handleId);
        return OS_EUNKNOWN;
    }

    if (resourceOut) {
        *resourceOut = handle->Resource;
    }
    return OS_EOK;
}

oserr_t
RegisterHandlePath(
        _In_ uuid_t      handleId,
        _In_ const char* path)
{
    struct ResourceHandle* handle;
    struct HandleMapping*  mapping;
    mstring_t*              internalPath;
    DEBUG("RegisterHandlePath(id=%u, path=%s)", handleId, path);

    internalPath = mstr_new_u8(path);

    // TODO do some actual verification of path here
    if (internalPath == NULL || mstr_len(internalPath) == 0) {
        return OS_EINVALPARAMS;
    }

    SpinlockAcquireIrq(&g_handlesLock);
    handle = __LookupSafe(handleId);
    if (handle == NULL) {
        SpinlockReleaseIrq(&g_handlesLock);
        mstr_delete(internalPath);
        return OS_ENOENT;
    }

    if (handle->Path) {
        SpinlockReleaseIrq(&g_handlesLock);
        mstr_delete(internalPath);
        return OS_EUNKNOWN;
    }

    mapping = hashtable_get(&g_handlemappings, &(struct HandleMapping) { .path = internalPath });
    if (mapping) {
        SpinlockReleaseIrq(&g_handlesLock);
        mstr_delete(internalPath);
        return OS_EEXISTS;
    }

    // store the new mapping, and update the handle instance
    hashtable_set(&g_handlemappings, &(struct HandleMapping) { .path = internalPath, .handle = handleId });
    handle->Path = internalPath;
    SpinlockReleaseIrq(&g_handlesLock);

    return OS_EOK;
}

oserr_t
LookupHandleByPath(
        _In_  const char* path,
        _Out_ uuid_t*     handleOut)
{
    struct HandleMapping* mapping;
    mstring_t*            internalPath;
    TRACE("LookupHandleByPath(%s)", path);

    internalPath = mstr_new_u8(path);

    // TODO do some actual verification of path here
    if (internalPath == NULL || mstr_len(internalPath) == 0) {
        return OS_EINVALPARAMS;
    }

    SpinlockAcquireIrq(&g_handlesLock);
    mapping = hashtable_get(&g_handlemappings, &(struct HandleMapping) { .path = internalPath });
    if (mapping && handleOut) {
        *handleOut = mapping->handle;
    }
    SpinlockReleaseIrq(&g_handlesLock);
    mstr_delete(internalPath);
    return mapping != NULL ? OS_EOK : OS_ENOENT;
}

void*
LookupHandleOfType(
        _In_ uuid_t       ID,
        _In_ HandleType_t type)
{
    struct ResourceHandle* handle;
    void*                  resource;

    SpinlockAcquireIrq(&g_handlesLock);
    handle = __LookupSafe(ID);
    if (handle == NULL || handle->Type != type) {
        SpinlockReleaseIrq(&g_handlesLock);
        return NULL;
    }

    resource = handle->Resource;
    SpinlockReleaseIrq(&g_handlesLock);
    return resource;
}

oserr_t
DestroyHandle(
        _In_ uuid_t handleId)
{
    struct ResourceHandle* handle;

    SpinlockAcquireIrq(&g_handlesLock);
    handle = __LookupSafe(handleId);
    if (handle == NULL) {
        SpinlockReleaseIrq(&g_handlesLock);
        return OS_ENOENT;
    }

    // do nothing if there still is active handles
    handle->References--;
    if (handle->References) {
        SpinlockReleaseIrq(&g_handlesLock);
        return OS_EINCOMPLETE;
    }

    // mark handle for destruction
    handle->Flags |= __HANDLE_FLAG_DESTROYING;
    if (handle->Path) {
        hashtable_remove(&g_handlemappings, &(struct HandleMapping) { .path = handle->Path });
    }
    SpinlockReleaseIrq(&g_handlesLock);

    queue_push(&g_cleanQueue, &handle->QueueHeader);
    SemaphoreSignal(&g_eventHandle, 1);
    return OS_EOK;
}

static void
__CleanupHandle(
        _In_ struct ResourceHandle* handle)
{
    if (handle->Destructor) {
        handle->Destructor(handle->Resource);
    }
    if (handle->Path) {
        mstr_delete(handle->Path);
    }

    SpinlockAcquireIrq(&g_handlesLock);
    hashtable_remove(&g_handles, &(struct ResourceHandle) { .ID = handle->ID });
    SpinlockReleaseIrq(&g_handlesLock);
}

_Noreturn static void
HandleJanitorThread(
    _In_Opt_ void* arg)
{
    element_t* element;
    _CRT_UNUSED(arg);
    
    for (;;) {
        SemaphoreWait(&g_eventHandle, NULL);

        element = queue_pop(&g_cleanQueue);
        while (element) {
            __CleanupHandle((struct ResourceHandle*)element);
            element = queue_pop(&g_cleanQueue);
        }
    }
}

static uint64_t mapping_hash(const void* element)
{
    const struct HandleMapping* entry = element;
    return mstr_hash(entry->path); // already unique identifier
}

static int mapping_cmp(const void* element1, const void* element2)
{
    const struct HandleMapping* lh = element1;
    const struct HandleMapping* rh = element2;
    return mstr_cmp(lh->path, rh->path);
}

static uint64_t handle_hash(const void* element)
{
    const struct ResourceHandle* entry = element;
    return entry->ID; // already unique identifier
}

static int handle_cmp(const void* element1, const void* element2)
{
    const struct ResourceHandle* lh = element1;
    const struct ResourceHandle* rh = element2;

    // return 0 on true, 1 on false
    return lh->ID == rh->ID ? 0 : 1;
}
