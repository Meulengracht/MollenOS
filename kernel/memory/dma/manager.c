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

static Mutex_t       g_managerRegistryLock = OS_MUTEX_INIT(MUTEX_FLAG_PLAIN);
static DMAManager_t* g_manager;

static void
__DetachClosedManager(
    _In_ DMAManager_t* manager)
{
    bool releaseRegistryReference = false;

    // Requires both UpdateLock and Lock to be released. Close and cancellation
    // can both reach here. Clear the registry pointer so only one call releases
    // the registry reference.
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
        ReleaseGlobalHandle(manager->ID);
    }
}

// Requires Lock. Close current bindings whenever the service is lost.
// Grant callbacks use this lock too, without waiting for UpdateLock.
static void
__CloseManagerLocked(
    _In_ DMAManager_t* manager)
{
    manager->State = DMAManagerClosed;

    foreach (node, &manager->Registry.Attachments) {
        DMAAttachment_t* attachment = node->value;
        if (attachment->Binding.Current != NULL) {
            DMABindingCloseLocked(attachment->Binding.Current);
        }
    }
}

static void
__CloseManager(
    _In_ void* resource)
{
    DMAManager_t* manager = resource;

    // Close without UpdateLock so a registration copying its result cannot
    // delay grant cleanup. Published attachments keep the registry reference
    // until reboot.
    MutexLock(&manager->Lock);
    __CloseManagerLocked(manager);
    MutexUnlock(&manager->Lock);
    __DetachClosedManager(manager);
}

static void
__DeleteManager(
    _In_ void* resource)
{
    DMAManager_t* manager = resource;

    // The registry keeps the manager alive while it still has attachments.
    assert(manager->State == DMAManagerClosed);
    assert(manager->Registry.Count == 0);
    assert(manager->PendingEvents.count == 0);
    MutexDestruct(&manager->UpdateLock);
    MutexDestruct(&manager->Lock);
    kfree(manager);
}

// Requires Lock. Check that the security group still exists separately, without Lock.
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

    // The service may guess the handle and close it as soon as CreateHandle
    // installs the grant. Hold Lock until the ID and references are ready so
    // the close callback cannot run during setup.
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

    // The manager was never added to the registry. Use the saved handle because
    // grant cleanup can schedule destruction before this function returns.
    DestroyHandle(handle);
    if (hasOperationReference) {
        ReleaseGlobalHandle(handle);
    }
    return status;
}

oserr_t
DMAManagerPublish(
    _In_ DMAManager_t* manager)
{
    MSContextSecurity_t* serviceContext;
    oserr_t              status;

    // Take a temporary group reference to check that destruction has not begun.
    // Release it after this check so the manager does not keep the group alive.
    status = MSContextAcquireSecurity(manager->Principal, &serviceContext);
    if (status != OS_EOK) {
        ERROR("DMAManagerPublish: MSContextAcquireSecurity for manager %u failed: %u",
              manager->ID, status);
        MutexLock(&manager->Lock);
        __CloseManagerLocked(manager);
        MutexUnlock(&manager->Lock);
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
    ReleaseGlobalHandle(serviceContext->GlobalHandleID);
    return status;
}

void
DMAManagerCancelCreate(
    _In_ DMAManager_t* manager)
{
    __CloseManager(manager);
    DestroyHandle(manager->ID);
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
        DMAManagerRelease(&manager);
        return OS_ECANCELLED;
    }

    MutexLock(&manager->Lock);
    status = __CheckManagerStateLocked(manager);
    MutexUnlock(&manager->Lock);
    if (status != OS_EOK) {
        ERROR("DMAManagerReference: __CheckManagerStateLocked for manager %u failed: %u",
              handle, status);
        ReleaseGlobalHandle(serviceContext->GlobalHandleID);
        DMAManagerRelease(&manager);
        return status;
    }
    ReleaseGlobalHandle(serviceContext->GlobalHandleID);

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
    ReleaseGlobalHandle(handle);
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

    // Version, Reserved and unused entries have already been checked. Each
    // range has three uint64_t fields with no padding, so memcmp is safe here.
    return memcmp(
        first->Ranges,
        second->Ranges,
        first->RangeCount * sizeof(struct SHMDeviceRange)
    ) == 0;
}

// Requires Lock. Keep UpdateLock held while using the returned attachment
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

// Requires Lock. Keep UpdateLock held while using the returned attachment.
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

// Requires Lock. Recheck events because they may change while the prepared
// update's ID is being copied to the caller.
// A NULL update is a registration retry and only requires a live manager.
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
        if (attachment->Event.Exhausted) {
            return OS_EOVERFLOW;
        }

        // A closed binding still blocks changes until its last reference is
        // released. Check the remaining entries so event sequence exhaustion
        // returns OS_EOVERFLOW even if an earlier entry was busy.
        if (attachment->Event.Queued || attachment->Binding.Current != NULL) {
            status = OS_EBUSY;
        }
    }
    return status;
}

// Requires Lock and UpdateLock. A retry does not need a free slot or a new ID.
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

    // The syscall checked the grant, but the manager can close without taking
    // UpdateLock. Check its state again before validating the copied parameters.
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
    status = __PrepareAttachmentRequestLocked(manager, parameters, registration, &attachmentID);
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

        status = DMARecoveryPrepareRegistration(manager, attachment, &registration->RecoveryUpdate);
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
        // The ID has been copied and the manager is still live. Update the
        // recovery groups and registry together under Lock so readers see
        // the complete registration.
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
