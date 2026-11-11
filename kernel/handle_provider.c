/**
 * Copyright, Philip Meulengracht
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
 * Handle Services
 * - Service registrations, restricted references and the queue of resources
 *   waiting for cleanup.
 *
 * Locking:
 * g_providerLock protects the registration state and the cleanup queues. Handle
 * callbacks are run outside of the handle-table locks, so they can take this
 * mutex. Releasing a global handle only schedules its destructor, so it never
 * calls back into this file while we hold the mutex.
 *
 * References:
 * Each resource holds a global reference to its registration. The registration
 * holds no usable references to its resources, so closing the last client
 * handle or token can queue the cleanup without creating a reference cycle.
 * When the registration handle is closed the service ends right away, but the
 * registration memory is kept until every resource has released it.
 */

#include <handles/provider.h>
#include <handles/handle.h>
#include <handles/global.h>
#include <handles/grants.h>
#include <handles/set.h>
#include <heap.h>
#include <ioset.h>
#include <memoryspace.h>
#include <mutex.h>
#include <limits.h>

#include "handle_private.h"

static Mutex_t g_providerLock = OS_MUTEX_INIT(MUTEX_FLAG_PLAIN);

static void
__FreeProviderResource(
    _In_ struct __ProviderResource* resource)
{
    ReleaseGlobalHandle(resource->Provider->ID);
    kfree(resource);
}

static void
__CloseProviderLocked(
    _In_ struct __HandleProvider* provider)
{
    struct __ProviderResource* resource;

    provider->Open = false;
    
    while ((resource = provider->Pending) != NULL) {
        provider->Pending = resource->Next;
        __FreeProviderResource(resource);
    }

    provider->PendingTail = NULL;
}

static void
__CloseProvider(
    _In_ void* payload)
{
    MutexLock(&g_providerLock);
    __CloseProviderLocked(payload);
    MutexUnlock(&g_providerLock);
}

static void
__DeleteProvider(
    _In_ void* context)
{
    struct __HandleProvider* provider = context;

    // By the time this runs, every resource has released its reference to the
    // registration. So nothing more can be posted to the queue.
    ReleaseGlobalHandle(provider->QueueID);
    kfree(provider);
}

static void
__PostProviderCleanup(
    _In_ struct __HandleProvider* provider)
{
    MarkHandle(provider->ID, IOSETIN);
}

static void
__DeleteProviderReference(
    _In_ void* context)
{
    struct __ProviderReference* reference = context;
    struct __ProviderResource*  resource = reference->Resource;
    struct __HandleProvider*    provider = resource->Provider;

    MutexLock(&g_providerLock);
    resource->References--;
    if (resource->References == 0) {
        if (!provider->Open || !resource->Published) {
            __FreeProviderResource(resource);
        } else {
            // The resource itself is used as the queue node, so the final close
            // can never fail because we are out of memory, or because the
            // userspace IPC queue is full.
            resource->Next = NULL;
            if (provider->PendingTail != NULL) {
                provider->PendingTail->Next = resource;
            } else {
                provider->Pending = resource;
                __PostProviderCleanup(provider);
            }
            provider->PendingTail = resource;
        }
    }
    MutexUnlock(&g_providerLock);
    kfree(reference);
}

static bool
__ProviderExists(
    _In_ struct __HandleProvider* provider)
{
    oserr_t status;

    if (!provider->Open) {
        return false;
    }
    
    // Group destruction is deferred as well, so the registration can still be
    // open while its group is being destroyed. Once that has started, tokens
    // held by other groups must not be able to import resources from it.
    status = AcquireGlobalHandle(
        provider->GroupID,
        HandleTypeMSContext,
        NULL
    );
    if (status != OS_EOK) {
        __CloseProviderLocked(provider);
        return false;
    }
    ReleaseGlobalHandle(provider->GroupID);
    return true;
}

static oserr_t
__GetProvider(
    _In_  uuid_t                    registrationID,
    _Out_ struct __HandleProvider** providerOut)
{
    MSContextSecurity_t*     security = MemorySpaceGetSecurityContext();
    struct __HandleProvider* provider;
    oserr_t                  status;

    if (security == NULL) {
        return OS_EPERMISSIONS;
    }

    status = AcquireHandleReference(
        registrationID,
        HandleTypeProvider,
        (void**)&provider
    );
    if (status != OS_EOK) {
        return status;
    }
    
    if (provider->GroupID != security->GlobalHandleID) {
        ReleaseGlobalHandle(registrationID);
        return OS_EPERMISSIONS;
    }
    
    *providerOut = provider;
    return OS_EOK;
}

oserr_t
HandleProviderRegister(
    _In_  uuid_t  notificationQueueID,
    _Out_ uuid_t* registrationOut)
{
    MSContextSecurity_t*     security = MemorySpaceGetSecurityContext();
    struct __HandleProvider* provider;
    struct ioset_event       event;
    uuid_t                   id;
    oserr_t                  status;

    if (security == NULL) {
        return OS_EPERMISSIONS;
    }

    // The caller must have a grant for the queue. We keep a reference to the
    // queue for as long as the registration exists, so cleanup can always be
    // posted to it.
    status = AcquireHandleReference(notificationQueueID, HandleTypeSet, NULL);
    if (status != OS_EOK) {
        return status;
    }

    provider = kmalloc(sizeof(struct __HandleProvider));
    if (provider == NULL) {
        ReleaseGlobalHandle(notificationQueueID);
        return OS_EOOM;
    }
    
    provider->GroupID = security->GlobalHandleID;
    provider->QueueID = notificationQueueID;
    provider->Open = true;
    provider->Pending = NULL;
    provider->PendingTail = NULL;

    // We must hold the mutex while the handle is created, otherwise another
    // thread in this group could close the new handle before we have stored
    // its ID.
    MutexLock(&g_providerLock);
    id = CreateHandle(
        HandleTypeProvider,
        __CloseProvider,
        __DeleteProvider,
        provider
    );
    if (id != UUID_INVALID) {
        provider->ID = id;
        
        event.events = IOSETIN;
        event.data.handle = id;
        
        status = ControlHandleSet(
            notificationQueueID,
            IOSET_ADD,
            id,
            &event
        );
    } else {
        status = OS_EOOM;
    }
    MutexUnlock(&g_providerLock);
    
    if (status != OS_EOK) {
        if (id != UUID_INVALID) {
            DestroyHandle(id);
        } else {
            kfree(provider);
            ReleaseGlobalHandle(notificationQueueID);
        }
        return status;
    }

    *registrationOut = id;
    return OS_EOK;
}

/**
 * @brief Creates a new reference to a resource, with its own access mask. The
 * reference is added to the grant table of the given group, which does not have
 * to be the calling one (see HandleProviderImport). The caller must hold
 * g_providerLock.
 *
 * @param resource        The resource to create a reference to.
 * @param accessMask      What the new reference allows.
 * @param securityContext The group that gets the new reference.
 * @param referenceOut    Receives the ID of the new reference.
 * @return OS_EOK, OS_EOVERFLOW if the resource has too many references, or
 *         OS_EOOM if the reference could not be created.
 */
static oserr_t
__CreateProviderReference(
    _In_  struct __ProviderResource* resource,
    _In_  uint32_t                   accessMask,
    _In_  MSContextSecurity_t*       securityContext,
    _Out_ uuid_t*                    referenceOut)
{
    struct __ProviderReference* reference;
    uuid_t                      id;

    if (resource->References == UINT_MAX) {
        return OS_EOVERFLOW;
    }

    reference = kmalloc(sizeof(struct __ProviderReference));
    if (reference == NULL) {
        return OS_EOOM;
    }

    reference->Resource = resource;
    reference->AccessMask = accessMask;
    
    id = CreateHandleInContext(
        HandleTypeProviderReference,
        NULL,
        __DeleteProviderReference,
        reference,
        securityContext
    );
    if (id == UUID_INVALID) {
        kfree(reference);
        return OS_EOOM;
    }
    
    resource->References++;
    *referenceOut = id;
    return OS_EOK;
}

oserr_t
HandleProviderCreate(
    _In_  uuid_t   registrationID,
    _In_  uint32_t accessMask,
    _Out_ uuid_t*  referenceOut)
{
    struct __HandleProvider*   provider;
    struct __ProviderResource* resource;
    uuid_t                     id;
    oserr_t                    status;

    if (registrationID == UUID_INVALID) {
        if (accessMask != 0) {
            return OS_EINVALPARAMS;
        }

        id = CreateHandle(HandleTypeGeneric, NULL, NULL, NULL);
        if (id == UUID_INVALID) {
            return OS_EOOM;
        }

        *referenceOut = id;
        return OS_EOK;
    }

    status = __GetProvider(registrationID, &provider);
    if (status != OS_EOK) {
        return status;
    }

    resource = kmalloc(sizeof(struct __ProviderResource));
    if (resource == NULL) {
        ReleaseGlobalHandle(registrationID);
        return OS_EOOM;
    }

    *resource = (struct __ProviderResource) {
        .Provider = provider
    };

    MutexLock(&g_providerLock);
    status = __ProviderExists(provider) ? OS_EOK : OS_ECANCELLED;
    if (status != OS_EOK) {
        goto error;
    }
    
    status = __CreateProviderReference(
        resource,
        accessMask,
        MemorySpaceGetSecurityContext(),
        &id
    );
    if (status != OS_EOK) {
        goto error;
    }

    // The ID of the first reference is also the ID the service uses for the
    // resource. IDs are never reused, so the service can keep using it after
    // this reference has been closed.
    resource->ID = id;
    resource->Published = true;

    *referenceOut = id;
    MutexUnlock(&g_providerLock);
    return status;

error:
    MutexUnlock(&g_providerLock);
    kfree(resource);
    ReleaseGlobalHandle(registrationID);
    return status;
}

oserr_t
HandleProviderReferenceForExport(
    _In_  uuid_t                     referenceID,
    _In_  const GrantTokenOptions_t* options,
    _Out_ HandleType_t*              typeOut)
{
    struct __ProviderReference* reference;
    oserr_t                     status;

    if (options->Data.Words[0] != 0 || options->Data.Words[1] != 0) {
        return OS_EINVALPARAMS;
    }

    status = AcquireHandleReference(
        referenceID,
        HandleTypeProviderReference,
        (void**)&reference
    );
    if (status != OS_EOK) {
        return status;
    }

    MutexLock(&g_providerLock);
    if (!__ProviderExists(reference->Resource->Provider)) {
        status = OS_ECANCELLED;
    } else if (((uint32_t)options->Scope & reference->AccessMask) != (uint32_t)options->Scope) {
        status = OS_EPERMISSIONS;
    }
    MutexUnlock(&g_providerLock);
    
    if (status != OS_EOK) {
        ReleaseGlobalHandle(referenceID);
    } else {
        *typeOut = HandleTypeProviderReference;
    }
    return status;
}

oserr_t
HandleProviderImport(
    _In_  const GrantToken_t*  token,
    _In_  MSContextSecurity_t* securityContext,
    _Out_ uuid_t*              referenceOut)
{
    struct __ProviderReference* source;
    uuid_t                      sourceID;
    oserr_t                     status;

    if (securityContext == NULL) {
        securityContext = MemorySpaceGetSecurityContext();
    }

    // A single-use token is used up by this call, even if we fail to create the
    // reference afterwards.
    status = GrantTokenImport(
        token,
        GrantTokenResourceTypeProvider,
        token->Options.Scope,
        securityContext,
        &sourceID
    );
    if (status != OS_EOK) {
        return status;
    }

    source = LookupGlobalHandle(sourceID, HandleTypeProviderReference);
    
    MutexLock(&g_providerLock);
    if (!__ProviderExists(source->Resource->Provider)) {
        status = OS_ECANCELLED;
    } else {
        status = __CreateProviderReference(
            source->Resource,
            (uint32_t)token->Options.Scope,
            securityContext,
            referenceOut
        );
    }
    MutexUnlock(&g_providerLock);

    ReleaseGlobalHandle(sourceID);
    return status;
}

oserr_t
HandleProviderValidate(
    _In_  uuid_t   registrationID,
    _In_  uint64_t groupID,
    _In_  uuid_t   referenceID,
    _In_  uint32_t accessMask,
    _Out_ uuid_t*  resourceOut)
{
    struct __HandleProvider*    provider;
    struct __ProviderReference* reference;
    MSContextSecurity_t*        security;
    oserr_t                     status;

    status = __GetProvider(registrationID, &provider);
    if (status != OS_EOK) {
        return status;
    }

    status = MSContextAcquireSecurity(groupID, &security);
    if (status != OS_EOK) {
        goto release;
    }
    
    // The group must stay alive while we read its grant table. After the lookup
    // we only need the reference object for the remaining checks, so the group
    // is released right away. Validation never adds anything to the grant table
    // of either process.
    status = AcquireHandleReferenceInContext(
        security,
        referenceID,
        HandleTypeProviderReference,
        (void**)&reference
    );
    ReleaseGlobalHandle((uuid_t)groupID);
    if (status != OS_EOK) {
        goto release;
    }
    
    MutexLock(&g_providerLock);
    if (reference->Resource->Provider != provider) {
        status = OS_EPERMISSIONS;
    } else if (!__ProviderExists(provider)) {
        status = OS_ECANCELLED;
    } else if ((accessMask & reference->AccessMask) != accessMask) {
        status = OS_EPERMISSIONS;
    } else {
        *resourceOut = reference->Resource->ID;
    }
    MutexUnlock(&g_providerLock);
    ReleaseGlobalHandle(referenceID);

release:
    ReleaseGlobalHandle(registrationID);
    return status;
}

oserr_t
HandleProviderGetEvents(
    _In_  uuid_t    registrationID,
    _Out_ uuid_t*   userIDs,
    _In_  uint32_t  capacity,
    _Out_ uint32_t* userCount)
{
    struct __HandleProvider*   provider;
    struct __ProviderResource* resource;
    uuid_t*                    ids = NULL;
    uint32_t                   count = 0;
    oserr_t                    status;

    status = __GetProvider(registrationID, &provider);
    if (status != OS_EOK) {
        return status;
    }

    MutexLock(&g_providerLock);
    
    if (!__ProviderExists(provider)) {
        status = OS_ECANCELLED;
        goto unlock;
    }

    if (capacity == 0 || (uint64_t)capacity * sizeof(uuid_t) > SIZE_MAX) {
        status = OS_EINVALPARAMS;
        goto unlock;
    }

    // The capacity comes from userspace and can be any value, so we size the
    // allocation by the number of pending entries instead. The queue nodes were
    // already allocated when each resource was created.
    for (resource = provider->Pending; resource != NULL && count < capacity; resource = resource->Next) {
        count++;
    }

    if (count != 0) {
        ids = kmalloc((size_t)count * sizeof(uuid_t));
        if (ids == NULL) {
            status = OS_EOOM;
            goto unlock;
        }
        
        resource = provider->Pending;
        
        for (uint32_t i = 0; i < count; i++) {
            ids[i] = resource->ID;
            resource = resource->Next;
        }

        // Copy the whole batch with a single MemorySpaceCopyToUser call. The
        // queue must stay unchanged until both copies are done. If a copy fails
        // it may leave partial output in userspace, but no entries are removed.
        status = MemorySpaceCopyToUser(userIDs, ids, (size_t)count * sizeof(uuid_t));
        if (status != OS_EOK) {
            goto unlock;
        }
    }

    status = MemorySpaceCopyToUser(userCount, &count, sizeof(count));
    if (status != OS_EOK) {
        goto unlock;
    }
    
    for (uint32_t i = 0; i < count; i++) {
        resource = provider->Pending;
        provider->Pending = resource->Next;
        __FreeProviderResource(resource);
    }

    if (provider->Pending == NULL) {
        provider->PendingTail = NULL;
    }

unlock:
    // Reading the queue uses up the wakeup. So if entries are still pending
    // after a partial or failed read, we post the wakeup again. The queue
    // merges it with any event that is already waiting.
    if (provider->Open && provider->Pending != NULL) {
        __PostProviderCleanup(provider);
    }
    MutexUnlock(&g_providerLock);
    kfree(ids);
    ReleaseGlobalHandle(registrationID);
    return status;
}

void
HandleProviderCancelCreate(
    _In_ uuid_t referenceID)
{
    struct __ProviderReference* reference;
    oserr_t                     status;

    status = AcquireHandleReference(referenceID, HandleTypeProviderReference, (void**)&reference);
    if (status != OS_EOK) {
        return;
    }

    MutexLock(&g_providerLock);
    reference->Resource->Published = false;
    MutexUnlock(&g_providerLock);
    ReleaseGlobalHandle(referenceID);
}
