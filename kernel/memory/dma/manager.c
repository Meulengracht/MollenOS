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
 */

#include <assert.h>
#include <debug.h>
#include <dma/address.h>
#include <handles/global.h>
#include <handles/handle.h>
#include <heap.h>
#include <memoryspace.h>
#include <string.h>

#include "attachment_private.h"
#include "../utils.h"

static Mutex_t       g_managerRegistryLock = OS_MUTEX_INIT(MUTEX_FLAG_PLAIN);
static DMAManager_t* g_manager;

static void
__DetachClosedManager(
    _In_ DMAManager_t* manager)
{
    bool releaseRegistryReference = false;

    // Call with both UpdateLock and manager->Lock unlocked. Both handle close and
    // failed creation can reach here, so clear g_manager under the locks to
    // make sure only one path drops the global registry reference.
    MutexLock(&g_managerRegistryLock);
    MutexLock(&manager->Lock);
    if (g_manager == manager && manager->State == DMAManagerClosed &&
        manager->Registry.Count == 0) {
        g_manager = NULL;
        releaseRegistryReference = true;
    }
    MutexUnlock(&manager->Lock);
    MutexUnlock(&g_managerRegistryLock);

    if (releaseRegistryReference) {
        __ReleaseMemoryHandleReference(manager->ID, __func__);
    }
}

// Hold manager->Lock. Block all recovery groups whenever the manager closes.
// Handle close callbacks use this lock too and must not wait for UpdateLock.
static void
__CloseManagerLocked(
    _In_ DMAManager_t* manager)
{
    manager->State = DMAManagerClosed;

    foreach (node, &manager->Registry.Groups) {
        DMARecoveryBlockLocked(manager, node->value);
    }
}

// Once the manager closes, its group list can no longer change. The registry
// reference keeps the manager and all groups alive while cancelling outside Lock.
static void
__CancelClosedManager(
    _In_ DMAManager_t* manager)
{
    DMARecoveryGroup_t* groups[DMA_MANAGER_MAX_ATTACHMENTS];
    uint32_t            count = 0;

    MutexLock(&manager->Lock);
    assert(manager->State == DMAManagerClosed);
    foreach (node, &manager->Registry.Groups) {
        groups[count++] = node->value;
    }
    MutexUnlock(&manager->Lock);
    
    for (uint32_t i = 0; i < count; i++) {
        DMARecoveryCancel(groups[i]);
    }
}

static void
__CloseManager(
    _In_ void* resource)
{
    DMAManager_t* manager = resource;

    // Run without UpdateLock so a registration stalled on copying to user space
    // cannot block handle cleanup. Any attachments still registered keep the
    // global registry reference alive until reboot.
    MutexLock(&manager->Lock);
    __CloseManagerLocked(manager);
    MutexUnlock(&manager->Lock);
    __CancelClosedManager(manager);
    __DetachClosedManager(manager);
}

static void
__DeleteManager(
    _In_ void* resource)
{
    DMAManager_t* manager = resource;

    // The global registry keeps the manager alive as long as attachments remain.
    assert(manager->State == DMAManagerClosed);
    assert(manager->Registry.Count == 0);
    assert(manager->Registry.Groups.count == 0);
    assert(manager->PendingEvents.count == 0);
    MutexDestruct(&manager->UpdateLock);
    MutexDestruct(&manager->Lock);
    kfree(manager);
}

// Hold manager->Lock. Check separately (without Lock) whether the service's
// security group is still alive.
static oserr_t
__CheckManagerStateLocked(
    _In_ DMAManager_t* manager)
{
    if (manager->State == DMAManagerPreparing) {
        return OS_EBUSY;
    }
    if (manager->State == DMAManagerClosed) {
        return OS_ECANCELLED;
    }
    return OS_EOK;
}

oserr_t
DMAManagerPrepareCreate(
    _Out_ DMAManager_t** managerOut,
    _Out_ uuid_t*        handleOut)
{
    MSContextSecurity_t* serviceContext;
    DMAManager_t*        manager;
    uuid_t               handle;
    oserr_t              status;
    bool                 hasOperationReference = false;
    uuid_t               releasedHandle;
    oserr_t              releaseStatus;

    serviceContext = MemorySpaceGetSecurityContext();
    if (serviceContext == NULL) {
        return OS_EPERMISSIONS;
    }

    manager = kmalloc(sizeof(DMAManager_t));
    if (manager == NULL) {
        return OS_EOOM;
    }

    memset(manager, 0, sizeof(DMAManager_t));
    manager->ID = UUID_INVALID;
    manager->Principal = (uint64_t)serviceContext->GlobalHandleID;
    manager->State = DMAManagerPreparing;
    manager->Registry.NextAttachmentID = 1;
    manager->Registry.NextRecoveryGroupID = 1;
    manager->Registry.NextBindingGeneration = 1;
    list_construct(&manager->Registry.Attachments);
    list_construct(&manager->Registry.Groups);
    list_construct(&manager->PendingEvents);
    MutexConstruct(&manager->Lock, MUTEX_FLAG_PLAIN);
    MutexConstruct(&manager->UpdateLock, MUTEX_FLAG_PLAIN);

    MutexLock(&g_managerRegistryLock);
    if (g_manager != NULL) {
        MutexUnlock(&g_managerRegistryLock);
        manager->State = DMAManagerClosed;
        __DeleteManager(manager);
        return OS_EBUSY;
    }

    // The caller could guess the handle ID and close it right after CreateHandle
    // installs it. Hold manager->Lock until the ID and references are set so
    // the close callback cannot run mid-setup.
    MutexLock(&manager->Lock);
    handle = CreateHandle(HandleTypeDMAManager, __CloseManager, __DeleteManager, manager);
    if (handle == UUID_INVALID) {
        MutexUnlock(&manager->Lock);
        MutexUnlock(&g_managerRegistryLock);
        manager->State = DMAManagerClosed;
        __DeleteManager(manager);
        return OS_EOOM;
    }

    manager->ID = handle;

    status = AcquireGlobalHandle(handle, HandleTypeDMAManager, NULL);
    if (status != OS_EOK) {
        ERROR("DMAManagerPrepareCreate: AcquireGlobalHandle operation reference for manager %u failed: %u",
              handle, status);
        goto error;
    }
    hasOperationReference = true;

    status = AcquireGlobalHandle(handle, HandleTypeDMAManager, NULL);
    if (status != OS_EOK) {
        ERROR("DMAManagerPrepareCreate: AcquireGlobalHandle registry reference for manager %u failed: %u",
              handle, status);
        goto error;
    }
    g_manager = manager;
    MutexUnlock(&manager->Lock);
    MutexUnlock(&g_managerRegistryLock);

    *managerOut = manager;
    *handleOut = handle;
    return OS_EOK;

error:
    manager->State = DMAManagerClosed;
    MutexUnlock(&manager->Lock);
    MutexUnlock(&g_managerRegistryLock);

    // The manager was never added to the global registry. Clean up via the saved
    // handle ID because handle cleanup can schedule deletion before this returns.
    releasedHandle = handle;
    releaseStatus = DestroyHandle(releasedHandle);

    // Another thread may have already closed this unpublished handle.
    if (releaseStatus != OS_EOK && releaseStatus != OS_EINCOMPLETE && releaseStatus != OS_ENOENT) {
        ERROR("%s: DestroyHandle %u failed: %u", __func__, releasedHandle, releaseStatus);
    }
    
    if (hasOperationReference) {
        __ReleaseMemoryHandleReference(handle, __func__);
    }
    return status;
}

oserr_t
DMAManagerPublish(
    _In_ DMAManager_t* manager)
{
    MSContextSecurity_t* serviceContext;
    oserr_t              status;

    // Take a temporary reference to the service's security group to make sure
    // it is not shutting down, then drop it so the manager does not keep the
    // security group alive.
    status = MSContextAcquireSecurity(manager->Principal, &serviceContext);
    if (status != OS_EOK) {
        ERROR("DMAManagerPublish: MSContextAcquireSecurity for manager %u failed: %u",
              manager->ID, status);
        MutexLock(&manager->Lock);
        __CloseManagerLocked(manager);
        MutexUnlock(&manager->Lock);
        __CancelClosedManager(manager);
        return OS_ECANCELLED;
    }

    MutexLock(&manager->Lock);
    if (manager->State == DMAManagerPreparing) {
        manager->State = DMAManagerLive;
        status = OS_EOK;
    } else {
        ERROR("DMAManagerPublish: manager %u is no longer preparing", manager->ID);
        status = OS_ECANCELLED;
    }
    MutexUnlock(&manager->Lock);

    __ReleaseMemoryHandleReference(
        serviceContext->GlobalHandleID,
        __func__
    );
    return status;
}

void
DMAManagerCancelCreate(
    _In_ DMAManager_t* manager)
{
    uuid_t  releasedHandle;
    oserr_t releaseStatus;

    __CloseManager(manager);

    releasedHandle = manager->ID;
    releaseStatus = DestroyHandle(releasedHandle);

    // Another thread may have already closed this unpublished handle.
    if (releaseStatus != OS_EOK && releaseStatus != OS_EINCOMPLETE && releaseStatus != OS_ENOENT) {
        ERROR("%s: DestroyHandle %u failed: %u", __func__, releasedHandle, releaseStatus);
    }
}

oserr_t
DMAManagerReference(
    _In_  uuid_t         handle,
    _Out_ DMAManager_t** managerOut)
{
    MSContextSecurity_t* caller;
    MSContextSecurity_t* serviceContext;
    DMAManager_t*        manager;
    oserr_t              status;

    caller = MemorySpaceGetSecurityContext();
    if (caller == NULL) {
        return OS_EPERMISSIONS;
    }

    status = AcquireHandleReference(handle, HandleTypeDMAManager, (void**)&manager);
    if (status != OS_EOK) {
        return status;
    }

    if (manager->Principal != (uint64_t)caller->GlobalHandleID) {
        DMAManagerRelease(&manager);
        return OS_EPERMISSIONS;
    }

    status = MSContextAcquireSecurity(manager->Principal, &serviceContext);
    if (status != OS_EOK) {
        ERROR("DMAManagerReference: MSContextAcquireSecurity for manager %u failed: %u",
              handle, status);
        MutexLock(&manager->Lock);
        __CloseManagerLocked(manager);
        MutexUnlock(&manager->Lock);
        __CancelClosedManager(manager);
        DMAManagerRelease(&manager);
        return OS_ECANCELLED;
    }

    MutexLock(&manager->Lock);
    status = __CheckManagerStateLocked(manager);
    MutexUnlock(&manager->Lock);
    if (status != OS_EOK) {
        ERROR("DMAManagerReference: __CheckManagerStateLocked for manager %u failed: %u",
              handle, status);
        __ReleaseMemoryHandleReference(serviceContext->GlobalHandleID, __func__);
        DMAManagerRelease(&manager);
        return status;
    }
    __ReleaseMemoryHandleReference(serviceContext->GlobalHandleID, __func__);

    *managerOut = manager;
    return OS_EOK;
}

void
DMAManagerRelease(
    _InOut_ DMAManager_t** manager)
{
    DMAManager_t* released = *manager;
    uuid_t        handle;

    if (released == NULL) {
        return;
    }

    *manager = NULL;

    handle = released->ID;
    __DetachClosedManager(released);
    __ReleaseMemoryHandleReference(handle, __func__);
}

static oserr_t
__ValidateAttachmentParameters(
    _In_ const OSDmaAttachmentParameters_t* parameters)
{
    oserr_t status;

    if (parameters->Version != DMA_ATTACHMENT_VERSION || parameters->Reserved != 0) {
        return OS_EINVALPARAMS;
    }

    if (parameters->Device == UUID_INVALID || parameters->Host == UUID_INVALID) {
        return OS_EINVALPARAMS;
    }

    if (parameters->RequesterGroup == 0 || parameters->ResetGroup == 0) {
        return OS_EINVALPARAMS;
    }

    if (parameters->CachePolicy != SHMDeviceCacheNonCoherent &&
        parameters->CachePolicy != SHMDeviceCacheCoherent) {
        return OS_EINVALPARAMS;
    }

    status = DMAAddressRangesValidate(parameters->Ranges, parameters->RangeCount);
    if (status != OS_EOK) {
        return status;
    }

    for (uint32_t i = parameters->RangeCount; i < DMA_MAX_RANGES; i++) {
        if (parameters->Ranges[i].PhysicalBase != 0 ||
            parameters->Ranges[i].DeviceBase != 0 || parameters->Ranges[i].Length != 0) {
            return OS_EINVALPARAMS;
        }
    }
    return OS_EOK;
}

static bool
__SameAttachmentConfiguration(
    _In_ const OSDmaAttachmentParameters_t* first,
    _In_ const OSDmaAttachmentParameters_t* second)
{
    if (first->Device != second->Device || first->Host != second->Host) {
        return false;
    }

    if (first->RequesterGroup != second->RequesterGroup ||
        first->ResetGroup != second->ResetGroup) {
        return false;
    }

    if (first->AddressLimit != second->AddressLimit || first->CachePolicy != second->CachePolicy ||
        first->RangeCount != second->RangeCount) {
        return false;
    }

    // Version, Reserved, and trailing zeroed entries were checked earlier. Each
    // SHMDeviceRange has three uint64_t fields with no padding, so memcmp is safe.
    return memcmp(
        first->Ranges,
        second->Ranges,
        first->RangeCount * sizeof(struct SHMDeviceRange)
    ) == 0;
}

// Hold manager->Lock, and keep UpdateLock held while using the returned pointer
// so another update cannot remove it.
static DMAAttachment_t*
__FindAttachmentByDevice(
    _In_ DMAManager_t* manager,
    _In_ uuid_t        deviceID)
{
    DMAAttachment_t* attachment;

    foreach (node, &manager->Registry.Attachments) {
        attachment = node->value;
        if (attachment->Configuration.Device == deviceID) {
            return attachment;
        }
    }
    return NULL;
}

// Hold manager->Lock, and keep UpdateLock held while using the returned pointer.
static DMAAttachment_t*
__FindAttachmentByID(
    _In_ DMAManager_t* manager,
    _In_ uint64_t      attachmentID)
{
    foreach (node, &manager->Registry.Attachments) {
        DMAAttachment_t* attachment = node->value;
        if (attachment->ID == attachmentID) {
            return attachment;
        }
    }
    return NULL;
}

// Hold manager->Lock. Recheck events because new events can arrive while copying
// a prepared ID to user space. A NULL update means this is a retry and only
// needs the manager to be live.
static oserr_t
__CheckRecoveryUpdateLocked(
    _In_ DMAManager_t*        manager,
    _In_ DMARecoveryUpdate_t* update)
{
    oserr_t status;

    status = __CheckManagerStateLocked(manager);
    if (status != OS_EOK || update == NULL) {
        return status;
    }

    for (uint32_t i = 0; i < update->Count; i++) {
        DMAAttachment_t* attachment = update->Entries[i].Attachment;
        if (attachment->Event.Sequence == UINT64_MAX) {
            return OS_EOVERFLOW;
        }

        // A closed binding still blocks group changes until its last handle
        // reference drops. Keep checking the rest of the entries so a full
        // event counter still reports OS_EOVERFLOW even if an earlier entry was busy.
        if (attachment->Event.Queued || attachment->Binding.Current != NULL) {
            status = OS_EBUSY;
        }

        if (attachment->RecoveryGroup != NULL) {
            DMARecoveryGroup_t* group = attachment->RecoveryGroup;

            MutexLock(&group->Lock);
            if (group->Gate.State == DMARecoveryBlocked ||
                group->Gate.State == DMARecoveryConfirmed ||
                group->Gate.Operations != 0 || group->Stop.Phase != DMARecoveryStopIdle) {
                status = OS_EBUSY;
            }
            MutexUnlock(&group->Lock);
        }
    }
    return status;
}

// Hold manager->Lock and UpdateLock. Retrying an identical setup needs neither
// a free registry slot nor a new attachment ID.
static oserr_t
__PrepareAttachmentRequestLocked(
    _In_    DMAManager_t*                      manager,
    _In_    const OSDmaAttachmentParameters_t* parameters,
    _InOut_ DMAAttachmentRegistration_t*       registration,
    _Out_   uint64_t*                          attachmentIDOut)
{
    DMAAttachment_t* existing;
    oserr_t          status;

    status = __CheckManagerStateLocked(manager);
    if (status != OS_EOK) {
        return status;
    }

    existing = __FindAttachmentByDevice(manager, parameters->Device);
    if (existing != NULL) {
        if (!__SameAttachmentConfiguration(&existing->Configuration, parameters)) {
            return OS_EBUSY;
        }

        registration->Attachment = existing;
        *attachmentIDOut = existing->ID;
        return OS_EOK;
    }

    if (manager->Registry.Count == DMA_MANAGER_MAX_ATTACHMENTS) {
        return OS_ENOTSUPPORTED;
    }

    if (manager->Registry.NextAttachmentID == UINT64_MAX) {
        return OS_EOVERFLOW;
    }

    registration->CreatesAttachment = true;
    *attachmentIDOut = manager->Registry.NextAttachmentID++;
    return OS_EOK;
}

oserr_t
DMAManagerPrepareAttachment(
    _In_  DMAManager_t*                      manager,
    _In_  const OSDmaAttachmentParameters_t* parameters,
    _Out_ DMAAttachmentRegistration_t**      registrationOut,
    _Out_ OSDmaAttachmentId_t*               attachmentOut)
{
    DMAAttachmentRegistration_t* registration;
    DMAAttachment_t*             attachment;
    uint64_t                     attachmentID;
    oserr_t                      status;

    // The syscall already checked the handle, but the manager can close without
    // waiting for UpdateLock. Recheck the state before validating parameters.
    MutexLock(&manager->UpdateLock);
    MutexLock(&manager->Lock);
    status = __CheckManagerStateLocked(manager);
    MutexUnlock(&manager->Lock);
    if (status != OS_EOK) {
        goto unlock;
    }

    status = __ValidateAttachmentParameters(parameters);
    if (status != OS_EOK) {
        goto unlock;
    }

    registration = kmalloc(sizeof(DMAAttachmentRegistration_t));
    if (registration == NULL) {
        status = OS_EOOM;
        goto unlock;
    }

    memset(registration, 0, sizeof(DMAAttachmentRegistration_t));
    registration->Manager = manager;

    MutexLock(&manager->Lock);
    status = __PrepareAttachmentRequestLocked(
        manager,
        parameters,
        registration,
        &attachmentID
    );
    MutexUnlock(&manager->Lock);
    if (status != OS_EOK) {
        goto cancel;
    }

    if (registration->CreatesAttachment) {
        attachment = kmalloc(sizeof(DMAAttachment_t));
        if (attachment == NULL) {
            status = OS_EOOM;
            goto cancel;
        }

        memset(attachment, 0, sizeof(DMAAttachment_t));
        ELEMENT_INIT(&attachment->Link, 0, attachment);
        ELEMENT_INIT(&attachment->Event.Link, 0, attachment);
        attachment->ID = attachmentID;
        attachment->Configuration = *parameters;
        registration->Attachment = attachment;

        status = DMARecoveryPrepareRegistration(
            manager,
            attachment,
            &registration->RecoveryUpdate
        );
        if (status != OS_EOK) {
            goto cancel;
        }
    }

    MutexLock(&manager->Lock);
    status = __CheckRecoveryUpdateLocked(manager, registration->RecoveryUpdate);
    MutexUnlock(&manager->Lock);
    if (status != OS_EOK) {
        goto cancel;
    }

    *attachmentOut = (OSDmaAttachmentId_t) { 
        .Value = attachmentID
    };
    *registrationOut = registration;
    return OS_EOK;

cancel:
    DMAManagerCancelAttachment(&registration);
    return status;

unlock:
    MutexUnlock(&manager->UpdateLock);
    return status;
}

oserr_t
DMAManagerPublishAttachment(
    _InOut_ DMAAttachmentRegistration_t** registration)
{
    DMAAttachmentRegistration_t* prepared = *registration;
    DMAManager_t*                manager = prepared->Manager;
    oserr_t                      status;

    MutexLock(&manager->Lock);
    status = __CheckRecoveryUpdateLocked(manager, prepared->RecoveryUpdate);
    if (status != OS_EOK) {
        ERROR("DMAManagerPublishAttachment: __CheckRecoveryUpdateLocked for manager %u failed: %u",
              manager->ID, status);
        MutexUnlock(&manager->Lock);
        return status;
    }

    if (prepared->CreatesAttachment) {
        // The ID copied cleanly and the manager is still live. Swap the new
        // recovery groups and attachment into the registry together under Lock
        // so readers never see a half-updated state.
        DMARecoveryApplyUpdate(prepared->RecoveryUpdate);
        list_append(&manager->Registry.Attachments, &prepared->Attachment->Link);
        manager->Registry.Count++;
    }
    MutexUnlock(&manager->Lock);

    DMARecoveryDiscardUpdate(&prepared->RecoveryUpdate);
    kfree(prepared);
    *registration = NULL;
    MutexUnlock(&manager->UpdateLock);
    return OS_EOK;
}

void
DMAManagerCancelAttachment(
    _InOut_ DMAAttachmentRegistration_t** registration)
{
    DMAAttachmentRegistration_t* prepared = *registration;
    DMAManager_t*                manager;

    if (prepared == NULL) {
        return;
    }

    manager = prepared->Manager;
    DMARecoveryDiscardUpdate(&prepared->RecoveryUpdate);
    if (prepared->CreatesAttachment && prepared->Attachment != NULL) {
        kfree(prepared->Attachment);
    }
    kfree(prepared);

    *registration = NULL;
    MutexUnlock(&manager->UpdateLock);
}

oserr_t
DMAManagerRemoveAttachment(
    _In_ DMAManager_t* manager,
    _In_ uint64_t      attachmentID)
{
    DMARecoveryUpdate_t* update = NULL;
    DMAAttachment_t*     removedAttachment;
    oserr_t              status;

    if (attachmentID == 0) {
        return OS_EINVALPARAMS;
    }

    MutexLock(&manager->UpdateLock);
    MutexLock(&manager->Lock);
    status = __CheckManagerStateLocked(manager);
    if (status != OS_EOK) {
        ERROR("DMAManagerRemoveAttachment: __CheckManagerStateLocked for manager %u failed: %u",
              manager->ID, status);
        goto unlock_registry;
    }

    removedAttachment = __FindAttachmentByID(manager, attachmentID);
    if (removedAttachment == NULL) {
        ERROR("DMAManagerRemoveAttachment: attachment %llu not found for manager %u",
              (unsigned long long)attachmentID, manager->ID);
        status = OS_ENOENT;
        goto unlock_registry;
    }
    MutexUnlock(&manager->Lock);

    status = DMARecoveryPrepareRemoval(manager, removedAttachment, &update);
    if (status != OS_EOK) {
        ERROR("DMAManagerRemoveAttachment: DMARecoveryPrepareRemoval for manager %u failed: %u",
              manager->ID, status);
        goto unlock;
    }

    MutexLock(&manager->Lock);
    status = __CheckRecoveryUpdateLocked(manager, update);
    if (status != OS_EOK) {
        ERROR("DMAManagerRemoveAttachment: __CheckRecoveryUpdateLocked for manager %u failed: %u",
              manager->ID, status);
        goto unlock_registry;
    }

    DMARecoveryApplyUpdate(update);
    list_remove(&manager->Registry.Attachments, &removedAttachment->Link);
    manager->Registry.Count--;
    MutexUnlock(&manager->Lock);
    kfree(removedAttachment);
    goto discard;

unlock_registry:
    MutexUnlock(&manager->Lock);

discard:
    DMARecoveryDiscardUpdate(&update);

unlock:
    MutexUnlock(&manager->UpdateLock);
    return status;
}
