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
 *
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
#include <heap.h>
#include <memoryspace.h>
#include <threading.h>

#include <handles/handle.h>
#include <handles/global.h>
#include <handles/paths.h>
#include <handles/init.h>
#include <handles/grants.h>
#include "handle_private.h"

// Handle is being destroyed
#define __HANDLE_FLAG_DESTROYING 0x1

// Handles are managed in two layers
// 1. The global handle table (g_handles) keeps track of all active handles.
// 2. The thread-cluster level grant table that keeps track of handles a thread
//    cluster has access to, and a level of references counting for those handles.
typedef struct HandleGrantsTable {
    hashtable_t Table;
    Spinlock_t  Lock;
} HandleGrantsTable_t;

struct ResourceHandle {
    element_t          QueueHeader;
    uuid_t             ID;
    HandleType_t       Type;
    unsigned int       Flags;
    mstring_t*         Path;
    void*              Resource;
    int                References;
    HandleDestructorFn GrantCleanup;
    HandleDestructorFn GlobalCleanup;
};

struct HandleGrantEntry {
    uuid_t                 ID;
    int                    References;
    struct ResourceHandle* Handle;
};

struct HandleEntry {
    uuid_t                 ID;
    struct ResourceHandle* Handle;
};

struct HandleMapping {
    mstring_t* path;
    uuid_t     handle;
};

_Noreturn static void HandleJanitorThread(void* arg);
static oserr_t  __DestroyGlobalHandle(uuid_t handleId);
static uint64_t mapping_hash(const void* element);
static int      mapping_cmp(const void* element1, const void* element2);
static uint64_t handle_hash(const void* element);
static int      handle_cmp(const void* element1, const void* element2);
static uint64_t grant_hash(const void* element);
static int      grant_cmp(const void* element1, const void* element2);

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
    // Initialize the global handle mappings
    // These are a way to register global names to lookup
    // certain handles.
    hashtable_construct(
        &g_handlemappings,
        HASHTABLE_MINIMUM_CAPACITY,
        sizeof(struct HandleMapping),
        mapping_hash,
        mapping_cmp
    );

    // Initialize the global handle table
    hashtable_construct(
        &g_handles,
        HASHTABLE_MINIMUM_CAPACITY,
        sizeof(struct HandleEntry),
        handle_hash,
        handle_cmp
    );
    SpinlockConstruct(&g_handlesLock);
    atomic_store(&g_nextHandleId, 1);
    return OS_EOK;
}

oserr_t
InitializeHandleJanitor(void)
{
    // Create the thread with all defaults
    return ThreadCreate(
        "janitor",
        HandleJanitorThread,
        NULL,
        0,
        UUID_INVALID,
        0,
        0,
        &g_janitorHandle
    );
}

HandleGrantsTable_t*
CreateHandleGrantsTable(void)
{
    HandleGrantsTable_t* table = kmalloc(sizeof(HandleGrantsTable_t));
    if (table == NULL) {
        return NULL;
    }

    if (hashtable_construct(
        &table->Table,
        HASHTABLE_MINIMUM_CAPACITY,
        sizeof(struct HandleGrantEntry),
        grant_hash,
        grant_cmp
    ) != 0) {
        kfree(table);
        return NULL;
    }

    // avoid shrinking the hashtable while interrupts are off
    table->Table.flags = HASHTABLE_FLAG_NO_SHRINK;
    SpinlockConstruct(&table->Lock);

    return table;
}

static void
__CleanupGrant(
    _In_ int         index,
    _In_ const void* element,
    _In_ void*       userContext)
{
    struct HandleGrantEntry* grant = (struct HandleGrantEntry*)element;
    (void)index;
    (void)userContext;

    if (grant == NULL) {
        return;
    }
    
    // every grant keeps a reference on a handle
    if (grant->Handle->GrantCleanup) {
        grant->Handle->GrantCleanup(grant->Handle->Resource);
    }

    // destroy the global handle reference
    __DestroyGlobalHandle(grant->ID);
}

void
DestroyHandleGrantsTable(
    _In_ HandleGrantsTable_t* table)
{
    if (table == NULL) {
        return;
    }

    // Remove every subscription this process has added to handle sets. Sets can
    // be shared with other processes and live on after this one is gone, so the
    // subscriptions must be removed while the table is still valid, and before
    // the grants are released (which may destroy the resources they watch).
    HandleSetsRemoveGrant(table, UUID_INVALID);
    hashtable_enumerate(&table->Table, __CleanupGrant, NULL);
    hashtable_destroy(&table->Table);
    kfree(table);
}

static inline struct ResourceHandle*
__LookupGlobalSafe(
    _In_ uuid_t ID)
{
    struct ResourceHandle* handle;
    struct HandleEntry*    entry;
    if (g_handles.capacity == 0) {
        return NULL;
    }

    entry = hashtable_get(&g_handles, &(struct HandleEntry) { .ID = ID });
    if (entry == NULL) {
        return NULL;
    }

    handle = entry->Handle;
    if (handle->Flags & __HANDLE_FLAG_DESTROYING) {
        return NULL;
    }
    return handle;
}

static struct ResourceHandle*
__AcquireHandleByGrant(
    _In_ MSContextSecurity_t* securityContext,
    _In_ uuid_t               handleId)
{
    struct HandleGrantEntry* entry;
    struct ResourceHandle*   handle;

    SpinlockAcquireIrq(&securityContext->HandleTable->Lock);
    entry = hashtable_get(
        &securityContext->HandleTable->Table,
        &(struct HandleGrantEntry) {
            .ID = handleId
        }
    );
    if (entry == NULL) {
        SpinlockReleaseIrq(&securityContext->HandleTable->Lock);
        return NULL;
    }
    
    if (entry->References == INT_MAX) {
        SpinlockReleaseIrq(&securityContext->HandleTable->Lock);
        return NULL;
    }
    entry->References++;
    handle = entry->Handle;
    SpinlockReleaseIrq(&securityContext->HandleTable->Lock);
    return handle;
}

oserr_t
HandleSetsReferenceGrant(
    _In_ uuid_t handle)
{
    MSContextSecurity_t*     security = MemorySpaceGetSecurityContext();
    struct HandleGrantEntry* entry;
    oserr_t                  status = OS_EOK;

    if (security == NULL) {
        return OS_ENOENT;
    }
    
    SpinlockAcquireIrq(&security->HandleTable->Lock);
    entry = hashtable_get(
        &security->HandleTable->Table,
        &(struct HandleGrantEntry) { .ID = handle }
    );
    if (entry == NULL) {
        status = OS_ENOENT;
    } else if (entry->References == INT_MAX) {
        status = OS_EOVERFLOW;
    } else {
        entry->References++;
    }
    SpinlockReleaseIrq(&security->HandleTable->Lock);
    return status;
}

static struct ResourceHandle*
__AcquireHandle(
    _In_ uuid_t handleId)
{
    struct ResourceHandle* handle;
    MSContextSecurity_t*   securityContext;

    securityContext = MemorySpaceGetSecurityContext();
    if (securityContext != NULL) {
        assert(securityContext->HandleTable != NULL);
        return __AcquireHandleByGrant(securityContext, handleId);
    }

    SpinlockAcquireIrq(&g_handlesLock);
    handle = __LookupGlobalSafe(handleId);
    if (handle == NULL) {
        SpinlockReleaseIrq(&g_handlesLock);
        return NULL;
    }

    handle->References++;
    SpinlockReleaseIrq(&g_handlesLock);
    return handle;
}

static bool
__InsertGlobalTable(
    _In_ struct HandleEntry* entry)
{
    void* existing;
    bool  inserted;

    // Insert the handle into the global handle table
    SpinlockAcquireIrq(&g_handlesLock);
    existing = hashtable_set(&g_handles, entry);
    assert(existing == NULL);

    inserted = hashtable_get(&g_handles, entry) != NULL;
    SpinlockReleaseIrq(&g_handlesLock);
    return inserted;
}

static void
__RemoveGlobalTable(
    _In_ struct HandleEntry* entry)
{
    // Remove the handle from the global handle table
    SpinlockAcquireIrq(&g_handlesLock);
    hashtable_remove(&g_handles, entry);
    SpinlockReleaseIrq(&g_handlesLock);
}

static oserr_t
__InsertGrantTable(
    _In_ MSContextSecurity_t* securityContext,
    _In_ struct HandleEntry*  baseEntry)
{
    struct HandleGrantEntry grantEntry = {
        .ID = baseEntry->ID,
        .Handle = baseEntry->Handle,
        .References = 1
    };
    oserr_t status = OS_EOK;

    SpinlockAcquireIrq(&securityContext->HandleTable->Lock);
    if (hashtable_get(&securityContext->HandleTable->Table, &grantEntry) != NULL) {
        status = OS_EEXISTS;
    } else {
        hashtable_set(&securityContext->HandleTable->Table, &grantEntry);
        if (hashtable_get(&securityContext->HandleTable->Table, &grantEntry) == NULL) {
            status = OS_EOOM;
        }
    }
    SpinlockReleaseIrq(&securityContext->HandleTable->Lock);
    return status;
}

uuid_t
AllocateHandleId(void)
{
    uuid_t id = atomic_load(&g_nextHandleId);
    uuid_t next;

    while (id != UUID_INVALID) {
        next = id == UINT32_MAX ? UUID_INVALID : id + 1;
        if (atomic_compare_exchange_weak(&g_nextHandleId, &id, next)) {
            return id;
        }
    }
    return UUID_INVALID;
}

uuid_t
CreateHandleInContext(
    _In_ HandleType_t         handleType,
    _In_ HandleDestructorFn   grantCleanup,
    _In_ HandleDestructorFn   globalCleanup,
    _In_ void*                resource,
    _In_ MSContextSecurity_t* securityContext)
{
    struct ResourceHandle* handle;
    struct HandleEntry     entry;
    bool                   inserted;
    uuid_t                 id;

    handle = kmalloc(sizeof(struct ResourceHandle));
    if (handle == NULL) {
        return UUID_INVALID;
    }

    id = AllocateHandleId();
    if (id == UUID_INVALID) {
        kfree(handle);
        return UUID_INVALID;
    }
    
    ELEMENT_INIT(&handle->QueueHeader, NULL, NULL);
    handle->ID            = id;
    handle->Type          = handleType;
    handle->Path          = NULL;
    handle->Resource      = resource;
    handle->GrantCleanup  = grantCleanup;
    handle->GlobalCleanup = globalCleanup;
    handle->References    = 1;
    handle->Flags         = 0;
    
    // setup the hashtable entry for this handle
    entry.ID = id;
    entry.Handle = handle;

    // Insert the handle into the global handle table
    inserted = __InsertGlobalTable(&entry);
    if (!inserted) {
        kfree(handle);
        return UUID_INVALID;
    }

    // With a security context, the grant tracks the initial reference.
    // Otherwise, the kernel caller must release it.
    if (securityContext != NULL && __InsertGrantTable(securityContext, &entry) != OS_EOK) {
        __RemoveGlobalTable(&entry);
        kfree(handle);
        return UUID_INVALID;
    }
    return id;
}

uuid_t
CreateHandle(
    _In_ HandleType_t       handleType,
    _In_ HandleDestructorFn grantCleanup,
    _In_ HandleDestructorFn globalCleanup,
    _In_ void*              resource)
{
    return CreateHandleInContext(
        handleType,
        grantCleanup,
        globalCleanup,
        resource,
        MemorySpaceGetSecurityContext()
    );
}

uuid_t
CreateGlobalHandle(
    _In_ HandleType_t       handleType,
    _In_ HandleDestructorFn cleanup,
    _In_ void*              resource)
{
    return CreateHandleInContext(handleType, NULL, cleanup, resource, NULL);
}

oserr_t
AcquireHandle(
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
        ERROR("AcquireHandle requested handle type %u, but handle was of type %u",
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
AcquireGlobalHandleUntyped(
    _In_  uuid_t        handleId,
    _Out_ HandleType_t* typeOut,
    _Out_ void**        resourceOut)
{
    struct ResourceHandle* handle;
    oserr_t                status = OS_EOK;

    SpinlockAcquireIrq(&g_handlesLock);
    handle = __LookupGlobalSafe(handleId);
    if (handle == NULL) {
        status = OS_ENOENT;
    } else if (handle->Type == HandleTypeSHMAccess) {
        // CPU sessions and DMA operations must finish in the security group
        // that created them.
        status = OS_EPERMISSIONS;
    } else if (handle->References == INT_MAX) {
        status = OS_EOVERFLOW;
    } else {
        handle->References++;
        if (typeOut != NULL) {
            *typeOut = handle->Type;
        }
        if (resourceOut != NULL) {
            *resourceOut = handle->Resource;
        }
    }
    SpinlockReleaseIrq(&g_handlesLock);
    return status;
}

oserr_t
AcquireGlobalHandle(
    _In_  uuid_t       handleId,
    _In_  HandleType_t handleType,
    _Out_ void**       resourceOut)
{
    struct ResourceHandle* handle;
    oserr_t                status = OS_EOK;

    SpinlockAcquireIrq(&g_handlesLock);
    handle = __LookupGlobalSafe(handleId);
    if (handle == NULL) {
        status = OS_ENOENT;
    } else if (handle->Type != handleType) {
        status = OS_EUNKNOWN;
    } else if (handle->Type == HandleTypeSHMAccess) {
        // CPU sessions and DMA operations must finish in the security group
        // that created them.
        status = OS_EPERMISSIONS;
    } else if (handle->References == INT_MAX) {
        status = OS_EOVERFLOW;
    } else {
        handle->References++;
        if (resourceOut != NULL) {
            *resourceOut = handle->Resource;
        }
    }
    SpinlockReleaseIrq(&g_handlesLock);
    return status;
}

static inline struct ResourceHandle*
__LookupGrantSafe(
    _In_ MSContextSecurity_t* securityContext,
    _In_ uuid_t               handleId)
{
    struct ResourceHandle*   handle = NULL;
    struct HandleGrantEntry* entry;

    entry = hashtable_get(
        &securityContext->HandleTable->Table,
        &(struct HandleGrantEntry) {
            .ID = handleId
        }
    );
    if (entry != NULL) {
        handle = entry->Handle;
    }
    return handle;
}

static oserr_t
__DestroyGlobalHandle(
    _In_ uuid_t handleId)
{
    struct ResourceHandle* handle;

    SpinlockAcquireIrq(&g_handlesLock);
    handle = __LookupGlobalSafe(handleId);
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

oserr_t
GrantHandle(
    _In_ MSContextSecurity_t* securityContext,
    _In_ uuid_t               handleId)
{
    MSContextSecurity_t*   currentContext;
    struct ResourceHandle* handle;
    struct HandleEntry     entry;
    oserr_t                status;

    if (securityContext == NULL) {
        return OS_EINVALPARAMS;
    }
    currentContext = MemorySpaceGetSecurityContext();
    if (currentContext == NULL) {
        return OS_ENOTSUPPORTED;
    }

    // The source grant keeps the resource allocated until the global reference
    // is counted. No path takes a grant lock while holding the global lock.
    SpinlockAcquireIrq(&currentContext->HandleTable->Lock);
    handle = __LookupGrantSafe(currentContext, handleId);
    if (handle == NULL) {
        SpinlockReleaseIrq(&currentContext->HandleTable->Lock);
        return OS_ENOENT;
    }

    if (handle->Type == HandleTypeSHMAccess || handle->Type == HandleTypeGrantToken ||
        handle->Type == HandleTypeProvider) {
        SpinlockReleaseIrq(&currentContext->HandleTable->Lock);
        return OS_EPERMISSIONS;
    }

    SpinlockAcquireIrq(&g_handlesLock);
    handle->References++;
    SpinlockReleaseIrq(&g_handlesLock);
    SpinlockReleaseIrq(&currentContext->HandleTable->Lock);

    entry.ID = handleId;
    entry.Handle = handle;
    
    status = __InsertGrantTable(securityContext, &entry);
    if (status != OS_EOK) {
        __DestroyGlobalHandle(handleId);
    }
    return status == OS_EEXISTS ? OS_EOK : status;
}

oserr_t
AcquireHandleReferenceInContext(
    _In_  MSContextSecurity_t* context,
    _In_  uuid_t               handleId,
    _In_  HandleType_t         handleType,
    _Out_ void**               resourceOut)
{
    struct ResourceHandle* handle = NULL;
    oserr_t                status = OS_EOK;

    if (context != NULL) {
        SpinlockAcquireIrq(&context->HandleTable->Lock);
        handle = __LookupGrantSafe(context, handleId);
    }

    SpinlockAcquireIrq(&g_handlesLock);
    if (context == NULL) {
        handle = __LookupGlobalSafe(handleId);
    }
    
    if (handle == NULL) {
        status = OS_ENOENT;
    } else if (handle->Type != handleType) {
        status = OS_EUNKNOWN;
    } else if (handle->Type == HandleTypeSHMAccess) {
        // Access resources cannot have users outside their creating group.
        status = OS_EPERMISSIONS;
    } else if (handle->References == INT_MAX) {
        status = OS_EOVERFLOW;
    } else {
        handle->References++;
        if (resourceOut != NULL) {
            *resourceOut = handle->Resource;
        }
    }
    SpinlockReleaseIrq(&g_handlesLock);

    if (context != NULL) {
        SpinlockReleaseIrq(&context->HandleTable->Lock);
    }
    return status;
}

oserr_t
AcquireHandleReference(
    _In_  uuid_t handleId,
    _In_  HandleType_t handleType,
    _Out_ void** resourceOut)
{
    return AcquireHandleReferenceInContext(
        MemorySpaceGetSecurityContext(), handleId, handleType, resourceOut
    );
}

oserr_t
ReleaseGlobalHandle(
    _In_ uuid_t handleId)
{
    return __DestroyGlobalHandle(handleId);
}

oserr_t
RegisterHandlePath(
    _In_ uuid_t      handleId,
    _In_ const char* path)
{
    struct ResourceHandle* handle;
    struct HandleMapping*  mapping;
    mstring_t*             internalPath;
    DEBUG("RegisterHandlePath(id=%u, path=%s)", handleId, path);

    internalPath = mstr_new_u8(path);

    // TODO do some actual verification of path here
    if (internalPath == NULL || mstr_len(internalPath) == 0) {
        return OS_EINVALPARAMS;
    }

    SpinlockAcquireIrq(&g_handlesLock);
    handle = __LookupGlobalSafe(handleId);
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
LookupHandle(
    _In_ uuid_t       ID,
    _In_ HandleType_t type)
{
    struct ResourceHandle* handle;
    void*                  resource;
    MSContextSecurity_t*   securityContext;

    securityContext = MemorySpaceGetSecurityContext();
    if (securityContext != NULL) {
        assert(securityContext->HandleTable != NULL);
        SpinlockAcquireIrq(&securityContext->HandleTable->Lock);
        handle = __LookupGrantSafe(securityContext, ID);
        if (handle == NULL || handle->Type != type) {
            SpinlockReleaseIrq(&securityContext->HandleTable->Lock);
            return NULL;
        }
        resource = handle->Resource;
        SpinlockReleaseIrq(&securityContext->HandleTable->Lock);
        return resource;
    }

    return LookupGlobalHandle(ID, type);
}

void*
LookupGlobalHandle(
    _In_ uuid_t       ID,
    _In_ HandleType_t type)
{
    struct ResourceHandle* handle;
    void*                  resource;

    SpinlockAcquireIrq(&g_handlesLock);
    handle = __LookupGlobalSafe(ID);
    if (handle == NULL || handle->Type != type) {
        SpinlockReleaseIrq(&g_handlesLock);
        return NULL;
    }

    resource = handle->Resource;
    SpinlockReleaseIrq(&g_handlesLock);
    return resource;
}

static oserr_t
__DestroyGrantHandle(
    _In_ MSContextSecurity_t* securityContext,
    _In_ uuid_t               handleId)
{
    struct HandleGrantEntry* entry;
    oserr_t                  oserr;
    struct ResourceHandle*   handle;

    SpinlockAcquireIrq(&securityContext->HandleTable->Lock);

    entry = hashtable_get(
        &securityContext->HandleTable->Table,
        &(struct HandleGrantEntry) {
            .ID = handleId
        }
    );
    if (!entry) {
        SpinlockReleaseIrq(&securityContext->HandleTable->Lock);
        return OS_ENOENT;
    }

    // store the handle pointer before we do anything else
    handle = entry->Handle;
    
    // do nothing if there still is active handles
    entry->References--;
    if (entry->References) {
        SpinlockReleaseIrq(&securityContext->HandleTable->Lock);
        return OS_EINCOMPLETE;
    }

    // remove the entry from the grant table
    hashtable_remove(
        &securityContext->HandleTable->Table,
        &(struct HandleGrantEntry) {
            .ID = handleId
        }
    );
    
    // The grant is gone from the table now, so no new subscriptions can be tied
    // to it. An add that is still in progress holds a temporary count on the
    // grant (see HandleSetsReferenceGrant), so we can't get here before it has
    // finished. We keep the table locked while the subscriptions are removed,
    // otherwise a new grant for the same handle could be added in the meantime,
    // and its new subscriptions would be removed as well.
    HandleSetsRemoveGrant(securityContext->HandleTable, handleId);
    SpinlockReleaseIrq(&securityContext->HandleTable->Lock);

    // Invoke the grant cleanup at this point before we potentially
    // destroy the global reference
    if (handle->GrantCleanup) {
        handle->GrantCleanup(handle->Resource);
    }

    // destroy the global handle at this point
    oserr = __DestroyGlobalHandle(handleId);
    if (oserr != OS_EOK && oserr != OS_EINCOMPLETE) {
        ERROR("__DestroyGrantHandle failed to destroy global handle: %d", oserr);
        return OS_EOK;
    }

    return OS_EOK;
}

oserr_t
DestroyHandleInContext(
    _In_ MSContextSecurity_t* securityContext,
    _In_ uuid_t               handleId)
{
    if (securityContext != NULL) {
        assert(securityContext->HandleTable != NULL);
        return __DestroyGrantHandle(securityContext, handleId);
    }
    return __DestroyGlobalHandle(handleId);
}

oserr_t
DestroyHandle(
    _In_ uuid_t handleId)
{
    return DestroyHandleInContext(
        MemorySpaceGetSecurityContext(),
        handleId
    );
}

static void
__CleanupHandle(
    _In_ struct ResourceHandle* handle)
{
    if (handle->GlobalCleanup) {
        handle->GlobalCleanup(handle->Resource);
    }
    
    if (handle->Path) {
        mstr_delete(handle->Path);
    }

    SpinlockAcquireIrq(&g_handlesLock);
    hashtable_remove(&g_handles, &(struct HandleEntry) { .ID = handle->ID });
    SpinlockReleaseIrq(&g_handlesLock);
    
    // Destructors can create or release more handles. The record stays stable
    // throughout those mutations and is freed only after its index is removed.
    kfree(handle);
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
    const struct HandleEntry* entry = element;
    return entry->ID; // already unique identifier
}

static int handle_cmp(const void* element1, const void* element2)
{
    const struct HandleEntry* lh = element1;
    const struct HandleEntry* rh = element2;

    // return 0 on true, 1 on false
    return lh->ID == rh->ID ? 0 : 1;
}

static uint64_t grant_hash(const void* element)
{
    const struct HandleGrantEntry* entry = element;
    return entry->ID; // already unique identifier
}

static int grant_cmp(const void* element1, const void* element2)
{
    const struct HandleGrantEntry* lh = element1;
    const struct HandleGrantEntry* rh = element2;

    // return 0 on true, 1 on false
    return lh->ID == rh->ID ? 0 : 1;
}
