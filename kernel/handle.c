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
#include <heap.h>
#include <threading.h>

struct resource_handle {
    uuid_t             id;
    HandleType_t       type;
    unsigned int       flags;
    mstring_t*         path;
    void*              resource;
    int                references;
    HandleDestructorFn destructor;
};

struct handle_mapping {
    mstring_t* path;
    uuid_t     handle;
};

struct handle_cleanup {
    element_t          header;
    mstring_t*         path;
    void*              resource;
    HandleDestructorFn destructor;
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
                        sizeof(struct handle_mapping), mapping_hash,
                        mapping_cmp);
    hashtable_construct(&g_handles, HASHTABLE_MINIMUM_CAPACITY,
                        sizeof(struct resource_handle), handle_hash,
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

static inline struct resource_handle*
LookupSafeHandleInstance(
        _In_ uuid_t handleId)
{
    struct resource_handle* handle;
    if (!atomic_load(&g_nextHandleId)) {
        return NULL;
    }

    SpinlockAcquireIrq(&g_handlesLock);
    handle = hashtable_get(&g_handles, &(struct resource_handle) { .id = handleId });
    SpinlockReleaseIrq(&g_handlesLock);
    return handle;
}

static struct resource_handle*
__AcquireHandle(
        _In_ uuid_t handleId)
{
    struct resource_handle* handle;
    if (!atomic_load(&g_nextHandleId)) {
        return NULL;
    }

    SpinlockAcquireIrq(&g_handlesLock);
    handle = hashtable_get(&g_handles, &(struct resource_handle) { .id = handleId });
    if (handle) {
        handle->references++;
    }
    SpinlockReleaseIrq(&g_handlesLock);
    return handle;
}

uuid_t
CreateHandle(
    _In_ HandleType_t       handleType,
    _In_ HandleDestructorFn destructor,
    _In_ void*              resource)
{
    struct resource_handle handle;

    handle.id         = atomic_fetch_add(&g_nextHandleId, 1);
    handle.type       = handleType;
    handle.path       = NULL;
    handle.resource   = resource;
    handle.destructor = destructor;
    handle.references = 1;
    handle.flags      = 0;

    SpinlockAcquireIrq(&g_handlesLock);
    hashtable_set(&g_handles, &handle);
    SpinlockReleaseIrq(&g_handlesLock);
    return handle.id;
}

oserr_t
AcquireHandle(
        _In_  uuid_t handleId,
        _Out_ void** resourceOut)
{
    struct resource_handle* handle = __AcquireHandle(handleId);
    if (!handle) {
        return OS_ENOENT;
    }
    
    if (resourceOut) {
        *resourceOut = handle->resource;
    }
    return OS_EOK;
}

oserr_t
AcquireHandleOfType(
        _In_  uuid_t       handleId,
        _In_  HandleType_t handleType,
        _Out_ void**       resourceOut)
{
    struct resource_handle* handle;

    handle = __AcquireHandle(handleId);
    if (!handle) {
        return OS_ENOENT;
    }

    if (handle->type != handleType) {
        ERROR("AcquireHandleOfType requested handle type %u, but handle was of type %u",
              handleType, handle->type);
        DestroyHandle(handleId);
        return OS_EUNKNOWN;
    }

    if (resourceOut) {
        *resourceOut = handle->resource;
    }
    return OS_EOK;
}

oserr_t
RegisterHandlePath(
        _In_ uuid_t      handleId,
        _In_ const char* path)
{
    struct resource_handle* handle;
    struct handle_mapping*  mapping;
    mstring_t*              internalPath;
    DEBUG("[handle_register_path] %u => %s", handleId, path);

    internalPath = mstr_new_u8(path);

    // TODO do some actual verification of path here
    if (internalPath == NULL || mstr_len(internalPath) == 0) {
        return OS_EINVALPARAMS;
    }

    SpinlockAcquireIrq(&g_handlesLock);
    handle = hashtable_get(&g_handles, &(struct resource_handle) { .id = handleId });
    if (!handle) {
        SpinlockReleaseIrq(&g_handlesLock);
        mstr_delete(internalPath);
        return OS_ENOENT;
    }

    if (handle->path) {
        SpinlockReleaseIrq(&g_handlesLock);
        mstr_delete(internalPath);
        return OS_EUNKNOWN;
    }

    mapping = hashtable_get(&g_handlemappings, &(struct handle_mapping) { .path = internalPath });
    if (mapping) {
        SpinlockReleaseIrq(&g_handlesLock);
        mstr_delete(internalPath);
        return OS_EEXISTS;
    }

    // store the new mapping, and update the handle instance
    hashtable_set(&g_handlemappings, &(struct handle_mapping) { .path = internalPath, .handle = handleId });
    handle->path = internalPath;
    SpinlockReleaseIrq(&g_handlesLock);

    return OS_EOK;
}

oserr_t
LookupHandleByPath(
        _In_  const char* path,
        _Out_ uuid_t*     handleOut)
{
    struct handle_mapping* mapping;
    mstring_t*             internalPath;
    TRACE("[handle_lookup_by_path] %s", path);

    internalPath = mstr_new_u8(path);

    // TODO do some actual verification of path here
    if (internalPath == NULL || mstr_len(internalPath) == 0) {
        return OS_EINVALPARAMS;
    }

    SpinlockAcquireIrq(&g_handlesLock);
    mapping = hashtable_get(&g_handlemappings, &(struct handle_mapping) { .path = internalPath });
    if (mapping && handleOut) {
        *handleOut = mapping->handle;
    }
    SpinlockReleaseIrq(&g_handlesLock);
    mstr_delete(internalPath);
    return mapping != NULL ? OS_EOK : OS_ENOENT;
}

void*
LookupHandleOfType(
        _In_ uuid_t       handleId,
        _In_ HandleType_t handleType)
{
    struct resource_handle* handle = LookupSafeHandleInstance(handleId);
    if (!handle || handle->type != handleType) {
        return NULL;
    }
    return handle->resource;
}

static void AddHandleToCleanup(
        _In_ void*              resource,
        _In_ HandleDestructorFn dctor,
        _In_ mstring_t*         path)
{
    struct handle_cleanup* cleanup;

    cleanup = kmalloc(sizeof(struct handle_cleanup));
    assert(cleanup != NULL);

    ELEMENT_INIT(&cleanup->header, NULL, cleanup);
    cleanup->path       = path;
    cleanup->resource   = resource;
    cleanup->destructor = dctor;

    queue_push(&g_cleanQueue, &cleanup->header);
    SemaphoreSignal(&g_eventHandle, 1);
}

oserr_t
DestroyHandle(
        _In_ uuid_t handleId)
{
    struct resource_handle* handle;
    void*                   resource;
    HandleDestructorFn      dctor;
    mstring_t*              path;

    SpinlockAcquireIrq(&g_handlesLock);
    handle = hashtable_get(&g_handles, &(struct resource_handle) { .id = handleId });
    if (!handle) {
        SpinlockReleaseIrq(&g_handlesLock);
        return OS_ENOENT;
    }

    // do nothing if there still is active handles
    handle->references--;
    if (handle->references) {
        SpinlockReleaseIrq(&g_handlesLock);
        return OS_EINCOMPLETE;
    }

    // store some resources before releaseing lock
    resource = handle->resource;
    dctor    = handle->destructor;
    path     = handle->path;
    if (path) {
        hashtable_remove(&g_handlemappings, &(struct handle_mapping) { .path = path });
    }
    hashtable_remove(&g_handles, &(struct resource_handle) { .id = handleId });
    SpinlockReleaseIrq(&g_handlesLock);

    AddHandleToCleanup(resource, dctor, path);
    return OS_EOK;
}

_Noreturn static void
HandleJanitorThread(
    _In_Opt_ void* arg)
{
    element_t*             element;
    struct handle_cleanup* cleanup;
    _CRT_UNUSED(arg);
    
    for (;;) {
        SemaphoreWait(&g_eventHandle, NULL);

        element = queue_pop(&g_cleanQueue);
        while (element) {
            cleanup = (struct handle_cleanup*)element->value;
            if (cleanup->destructor) {
                cleanup->destructor(cleanup->resource);
            }
            if (cleanup->path) {
                mstr_delete(cleanup->path);
            }
            kfree(cleanup);

            element = queue_pop(&g_cleanQueue);
        }
    }
}

static uint64_t mapping_hash(const void* element)
{
    const struct handle_mapping* entry = element;
    return mstr_hash(entry->path); // already unique identifier
}

static int mapping_cmp(const void* element1, const void* element2)
{
    const struct handle_mapping* lh = element1;
    const struct handle_mapping* rh = element2;
    return mstr_cmp(lh->path, rh->path);
}

static uint64_t handle_hash(const void* element)
{
    const struct resource_handle* entry = element;
    return entry->id; // already unique identifier
}

static int handle_cmp(const void* element1, const void* element2)
{
    const struct resource_handle* lh = element1;
    const struct resource_handle* rh = element2;

    // return 0 on true, 1 on false
    return lh->id == rh->id ? 0 : 1;
}
