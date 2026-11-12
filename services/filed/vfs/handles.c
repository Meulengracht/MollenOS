/**
 * Copyright 2022, Philip Meulengracht
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
 */

#include <ddk/utils.h>
#include <ds/hashtable.h>
#include <gracht/server.h>
#include <internal/_service.h>
#include <internal/_tls.h>
#include <os/grants.h>
#include <os/handle.h>
#include <os/notification_queue.h>
#include <os/usched/job.h>
#include <vfs/vfs.h>
#include "private.h"

static uint64_t __HandlesHash(const void* element);
static int      __HandlesCmp(const void* lh, const void* rh);

static hashtable_t          g_handles;
static struct usched_rwlock g_handlesLock;
static OSHandle_t          g_registration;
static OSHandle_t          g_cleanupQueue;

static void
__CleanupJob(
    _In_ void* argument,
    _In_ void* cancellationToken)
{
    struct ioset_event event;
    OSAsyncContext_t*  asyncContext;
    uuid_t             resourceIDs[32];
    uint32_t           count;
    int                eventCount;
    oserr_t            oserr;

    _CRT_UNUSED(argument);
    _CRT_UNUSED(cancellationToken);
    asyncContext = __tls_current()->async_context;

    for (;;) {
        if (asyncContext != NULL) {
            OSAsyncContextInitialize(asyncContext);
        }

        oserr = OSNotificationQueueWait(
            &g_cleanupQueue,
            &event,
            1,
            0,
            NULL,
            &eventCount,
            asyncContext
        );
        if (oserr != OS_EOK) {
            if (oserr != OS_EINTERRUPTED) {
                WARNING("file cleanup wait failed: %u", oserr);
            }
            usched_job_yield();
            continue;
        }

        // Wakeups only indicate pending work. Drain the IDs, including batches
        // larger than the event queue, and tolerate a stale empty wakeup.
        do {
            oserr = OSHandleReadCleanup(&g_registration, resourceIDs, 32, &count);
            if (oserr != OS_EOK) {
                WARNING("file cleanup read failed: %u", oserr);
                usched_job_yield();
                break;
            }

            for (uint32_t i = 0; i < count; i++) {
                oserr = VFSNodeClose(NULL, resourceIDs[i]);
                if (oserr != OS_EOK && oserr != OS_ENOENT) {
                    WARNING("file cleanup failed for %u: %u", resourceIDs[i], oserr);
                }
            }
        } while (count != 0);
    }
}

oserr_t
VFSNodeHandleStartup(void)
{
    oserr_t oserr;

    usched_rwlock_init(&g_handlesLock);
    if (hashtable_construct(&g_handles, 0, sizeof(struct VFSNodeHandle),
                            __HandlesHash, __HandlesCmp) != 0) {
        return OS_EOOM;
    }

    oserr = OSNotificationQueueCreate(0, &g_cleanupQueue);
    if (oserr != OS_EOK) {
        goto error;
    }
    
    oserr = OSHandleRegisterProvider(&g_cleanupQueue, &g_registration);
    if (oserr != OS_EOK) {
        goto queue;
    }

    if (usched_job_queue(__CleanupJob, NULL) == UUID_INVALID) {
        oserr = OS_EOOM;
        OSHandleDestroy(&g_registration);
        goto queue;
    }
    return OS_EOK;

queue:
    OSHandleDestroy(&g_cleanupQueue);

error:
    hashtable_destroy(&g_handles);
    return oserr;
}

oserr_t
VFSNodeHandleCreate(
    _In_  uint32_t    accessKind,
    _Out_ OSHandle_t* handleOut)
{
    return OSHandleCreate(
        &g_registration,
        accessKind & (__FILE_READ_ACCESS | __FILE_WRITE_ACCESS),
        handleOut
    );
}

oserr_t
VFSNodeHandleValidate(
    _In_  const struct gracht_security_context* caller,
    _In_  uuid_t                                referenceID,
    _In_  uint32_t                              requiredAccess,
    _Out_ uuid_t*                               resourceIDOut)
{
    if (!__IsAuthenticatedCaller(caller)) {
        return OS_EPERMISSIONS;
    }
    return OSHandleValidate(
        &g_registration, caller->principal, referenceID, requiredAccess, resourceIDOut
    );
}

oserr_t
VFSNodeHandleExport(
    _In_  uuid_t                                resourceID,
    _In_  const struct gracht_security_context* caller,
    _Out_ GrantToken_t*                         tokenOut)
{
    struct VFSNodeHandle* handle;
    OSHandle_t            initialReference;
    GrantTokenOptions_t   options = { 0 };
    oserr_t               oserr;

    // Tokens currently address security groups with uuid_t. Reject narrowing
    // rather than issuing a token for a different group.
    if (!__IsAuthenticatedCaller(caller)) {
        return OS_EPERMISSIONS;
    }

    if (caller->principal > UINT32_MAX) {
        return OS_EPERMISSIONS;
    }

    oserr = OSHandlesFind(resourceID, &initialReference);
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = VFSNodeHandleGet(resourceID, &handle);
    if (oserr != OS_EOK) {
        return oserr;
    }

    options.ResourceType = GrantTokenResourceTypeProvider;
    options.Scope = handle->AccessKind & (__FILE_READ_ACCESS | __FILE_WRITE_ACCESS);
    options.TargetType = GrantTokenTargetTypeSpecific;
    options.TargetGlobalHandleID = (uuid_t)caller->principal;
    options.Flags = GrantTokenFlagsSingleUse;
    
    oserr = OSGrantTokenCreate(resourceID, &options, tokenOut);
    if (oserr == OS_EOK) {
        // Store only the control ID. The token keeps the resource in use until
        // import; keeping the first resource reference would prevent cleanup.
        handle->TokenControlHandleID = tokenOut->ControlHandleID;
        oserr = OSHandleRelease(&initialReference);
        if (oserr != OS_EOK) {
            (void)OSGrantTokenRevoke(tokenOut->ControlHandleID);
            handle->TokenControlHandleID = UUID_INVALID;
        }
    }
    VFSNodeHandlePut(handle);
    return oserr;
}

oserr_t
VFSNodeHandleAdd(
    _In_ OSHandle_t*     handle,
    _In_ struct VFSNode* node,
    _In_ void*           data,
    _In_ uint32_t        accessKind)
{
    void* existing;

    usched_rwlock_w_lock(&g_handlesLock);
    existing = hashtable_get(&g_handles, &(struct VFSNodeHandle) { .Id = handle->ID });
    if (existing != NULL) {
        usched_rwlock_w_unlock(&g_handlesLock);
        return OS_EEXISTS;
    }

    hashtable_set(&g_handles, &(struct VFSNodeHandle) {
        .Id = handle->ID,
        .Position = 0,
        .Data = data,
        .Node = node,
        .AccessKind = accessKind,
        .Mode = MODE_NONE
    });
    existing = hashtable_get(&g_handles, &(struct VFSNodeHandle) { .Id = handle->ID });
    usched_rwlock_w_unlock(&g_handlesLock);
    return existing != NULL ? OS_EOK : OS_EOOM;
}

oserr_t
VFSNodeHandleRemove(
        _In_ uuid_t handleId)
{
    struct VFSNodeHandle* handle;

    usched_rwlock_w_promote(&g_handlesLock);
    handle = hashtable_get(&g_handles, &(struct VFSNodeHandle) { .Id = handleId });
    if (handle == NULL) {
        usched_rwlock_w_demote(&g_handlesLock);
        return OS_ENOENT;
    }
    hashtable_remove(&g_handles, handle);
    usched_rwlock_w_demote(&g_handlesLock);
    return OS_EOK;
}

oserr_t
VFSNodeHandleGet(
        _In_  uuid_t                 handleId,
        _Out_ struct VFSNodeHandle** handleOut)
{
    struct VFSNodeHandle* handle;

    usched_rwlock_r_lock(&g_handlesLock);
    handle = hashtable_get(&g_handles, &(struct VFSNodeHandle) { .Id = handleId });
    if (handle == NULL) {
        usched_rwlock_r_unlock(&g_handlesLock);
        return OS_ENOENT;
    }
    *handleOut = handle;
    return OS_EOK;
}

void
VFSNodeHandlePut(
        _In_ struct VFSNodeHandle* handle)
{
    if (handle == NULL) {
        return;
    }
    usched_rwlock_r_unlock(&g_handlesLock);
}

static uint64_t __HandlesHash(const void* element)
{
    const struct VFSNodeHandle* handle = element;
    return handle->Id;
}

static int __HandlesCmp(const void* lh, const void* rh)
{
    const struct VFSNodeHandle* handle1 = lh;
    const struct VFSNodeHandle* handle2 = rh;
    return handle1->Id == handle2->Id ? 0 : 1;
}
