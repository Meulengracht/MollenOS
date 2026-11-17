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
 * DMA manager and attachment registry. Handles creating and closing the
 * manager handle, validating device configurations, and preparing, publishing,
 * or removing direct and translated device attachments.
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

void
DMAManagerDetachClosed(
    _In_ DMAManager_t* manager)
{
    bool releaseRegistryReference = false;

    // Call with both UpdateLock and manager->Lock unlocked. Both handle close and
    // failed creation can reach here, so clear g_manager under the locks to
    // make sure only one path drops the global registry reference. The
    // reference is only dropped once the manager is closed and has no
    // attachments and no domains left.
    MutexLock(&g_managerRegistryLock);
    MutexLock(&manager->Lock);
    if (g_manager == manager && manager->State == DMAManagerClosed &&
        manager->Registry.Count == 0 && manager->DomainCount == 0) {
        g_manager = NULL;
        releaseRegistryReference = true;
    }
    MutexUnlock(&manager->Lock);
    MutexUnlock(&g_managerRegistryLock);

    if (releaseRegistryReference) {
        TRACE("DMAManagerDetachClosed: manager %u dropped from the global registry", manager->ID);
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

    TRACE("__CloseManager: closing manager %u", manager->ID);

    // Run without UpdateLock so a registration stalled on copying to user space
    // cannot block handle cleanup. Any attachments still registered keep the
    // global registry reference alive until reboot. Registered domains do too.
    MutexLock(&manager->Lock);
    __CloseManagerLocked(manager);
    MutexUnlock(&manager->Lock);
    __CancelClosedManager(manager);
    DMAManagerDetachClosed(manager);
}

static void
__DeleteManager(
    _In_ void* resource)
{
    DMAManager_t* manager = resource;

    // The global registry holds a manager reference while any attachment or
    // domain exists. Both must be gone by the time the manager is deleted.
    assert(manager->State == DMAManagerClosed);
    assert(manager->Registry.Count == 0);
    assert(manager->DomainCount == 0 && manager->Domains.count == 0);
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

// Allocate a manager in the Preparing state, with empty lists and ID counters
// that start at 1. Nothing is registered and no handle exists yet. Returns NULL
// if the allocation fails.
static DMAManager_t*
__AllocateManager(
    _In_ const MSContextSecurity_t* serviceContext)
{
    DMAManager_t* manager;

    manager = kmalloc(sizeof(DMAManager_t));
    if (manager == NULL) {
        return NULL;
    }

    memset(manager, 0, sizeof(DMAManager_t));
    manager->ID = UUID_INVALID;
    manager->OwnerSecurityID = (uint64_t)serviceContext->GlobalHandleID;
    manager->State = DMAManagerPreparing;
    manager->Registry.NextAttachmentID = 1;
    manager->Registry.NextRecoveryGroupID = 1;
    manager->Registry.NextBindingGeneration = 1;
    list_construct(&manager->Registry.Attachments);
    list_construct(&manager->Registry.Groups);
    list_construct(&manager->PendingEvents);
    list_construct(&manager->Domains);
    
    // Start at 0 so the first check skips scanning when 
    // no IOMMU unit has failed.
    atomic_init(&manager->CheckedFailureSequence, 0);
    MutexConstruct(&manager->Lock, MUTEX_FLAG_PLAIN);
    MutexConstruct(&manager->UpdateLock, MUTEX_FLAG_PLAIN);
    return manager;
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
        WARNING("DMAManagerPrepareCreate: manager create denied, the caller has no security context");
        return OS_EPERMISSIONS;
    }

    manager = __AllocateManager(serviceContext);
    if (manager == NULL) {
        ERROR("DMAManagerPrepareCreate: failed to allocate a manager for security group %u",
              serviceContext->GlobalHandleID);
        return OS_EOOM;
    }

    MutexLock(&g_managerRegistryLock);
    if (g_manager != NULL) {
        WARNING("DMAManagerPrepareCreate: manager create refused, manager %u already exists", g_manager->ID);
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
        ERROR("DMAManagerPrepareCreate: failed to create a handle for the manager of security group %u",
              serviceContext->GlobalHandleID);
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

    TRACE("DMAManagerPrepareCreate: manager %u created, waiting to be published", handle);
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
    status = MSContextAcquireSecurity(manager->OwnerSecurityID, &serviceContext);
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
    if (status == OS_EOK) {
        TRACE("DMAManagerPublish: manager %u is live", manager->ID);
    }

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
        WARNING("DMAManagerReference: manager %u reference denied, the caller has no security context", handle);
        return OS_EPERMISSIONS;
    }

    status = AcquireHandleReference(handle, HandleTypeDMAManager, (void**)&manager);
    if (status != OS_EOK) {
        WARNING("DMAManagerReference: AcquireHandleReference for manager %u failed: %u", handle, status);
        return status;
    }

    if (manager->OwnerSecurityID != (uint64_t)caller->GlobalHandleID) {
        WARNING("DMAManagerReference: manager %u reference denied, security group %u is not the owner",
                handle, caller->GlobalHandleID);
        DMAManagerRelease(&manager);
        return OS_EPERMISSIONS;
    }

    status = MSContextAcquireSecurity(manager->OwnerSecurityID, &serviceContext);
    if (status != OS_EOK) {
        ERROR("DMAManagerReference: MSContextAcquireSecurity for manager %u failed: %u", handle, status);
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
        WARNING("DMAManagerReference: manager %u is not accepting requests: %u", handle, status);
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
    DMAManagerDetachClosed(released);
    __ReleaseMemoryHandleReference(handle, __func__);
}

static oserr_t
__ValidateAttachmentParameters(
    _In_ const OSDmaAttachmentParameters_t* parameters,
    _In_ bool                              translated)
{
    oserr_t status;

    if (parameters->Version != DMA_ATTACHMENT_VERSION || parameters->Reserved != 0) {
        WARNING("__ValidateAttachmentParameters: attachment for device %u rejected, bad version %u or reserved value %u",
                parameters->Device, parameters->Version, parameters->Reserved);
        return OS_EINVALPARAMS;
    }

    if (parameters->Device == UUID_INVALID || parameters->Host == UUID_INVALID) {
        WARNING("__ValidateAttachmentParameters: attachment rejected, invalid device %u or host %u",
                parameters->Device, parameters->Host);
        return OS_EINVALPARAMS;
    }

    if (parameters->RequesterGroup == 0 || parameters->ResetGroup == 0) {
        WARNING("__ValidateAttachmentParameters: attachment for device %u rejected, requester group %llu or reset group %llu is zero",
                parameters->Device, (unsigned long long)parameters->RequesterGroup,
                (unsigned long long)parameters->ResetGroup);
        return OS_EINVALPARAMS;
    }

    if (parameters->CachePolicy != SHMDeviceCacheNonCoherent &&
        parameters->CachePolicy != SHMDeviceCacheCoherent) {
        WARNING("__ValidateAttachmentParameters: attachment for device %u rejected, bad cache policy %u",
                parameters->Device, parameters->CachePolicy);
        return OS_EINVALPARAMS;
    }

    if (translated) {
        // A domain member uses the address space of its domain, so it has no
        // address ranges of its own.
        if (parameters->RangeCount != 0) {
            WARNING("__ValidateAttachmentParameters: attachment for domain device %u rejected, it lists %u ranges",
                    parameters->Device, parameters->RangeCount);
            return OS_EINVALPARAMS;
        }
    } else {
        status = DMAAddressRangesValidate(parameters->Ranges, parameters->RangeCount);
        if (status != OS_EOK) {
            WARNING("__ValidateAttachmentParameters: attachment for device %u rejected, %u ranges are invalid: %u",
                    parameters->Device, parameters->RangeCount, status);
            return status;
        }
    }

    for (uint32_t i = parameters->RangeCount; i < DMA_MAX_RANGES; i++) {
        if (parameters->Ranges[i].PhysicalBase != 0 ||
            parameters->Ranges[i].DeviceBase != 0 || parameters->Ranges[i].Length != 0) {
            WARNING("__ValidateAttachmentParameters: attachment for device %u rejected, unused range %u is not zero",
                    parameters->Device, i);
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
            ERROR("__CheckRecoveryUpdateLocked: event sequence of attachment %llu in manager %u is exhausted",
                  (unsigned long long)attachment->ID, manager->ID);
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
            if (group->Access.State == DMARecoveryBlocked ||
                group->Access.State == DMARecoveryStopConfirmed ||
                group->Access.Operations != 0 || group->Stop.Phase != DMARecoveryStopIdle) {
                status = OS_EBUSY;
            }
            MutexUnlock(&group->Lock);
        }
    }
    return status;
}

// Hold manager->Lock and UpdateLock. Checks whether an attachment fits with
// registered domains. For a translated attachment, selected is the domain the
// device is joining. That domain must belong to this manager, be Ready and
// Connected, list the device, and sit on a healthy IOMMU unit.
//
// For a direct attachment, selected is NULL. A direct device cannot share a
// RequesterGroup or device ID with any domain, including a Closing domain whose
// cleanup failed, because Closing domains keep their IDs reserved until gone.
// Returns OS_EINVALPARAMS for a wrong manager or unlisted device, OS_EBUSY for
// a Closing/unconnected domain or reserved ID, or the unit failure from
// DMAAddressSpaceCheck.
static oserr_t
__CheckAttachmentDomainLocked(
    _In_ DMAManager_t*                      manager,
    _In_ DMATranslatedDomain_t*             selected,
    _In_ const OSDmaAttachmentParameters_t* parameters)
{
    oserr_t status;

    if (selected != NULL) {
        if (selected->Manager != manager) {
            ERROR("__CheckAttachmentDomainLocked: domain %p does not belong to manager %u, device %u",
                  (void*)selected, manager->ID, parameters->Device);
            return OS_EINVALPARAMS;
        }

        if (selected->State != DMATranslatedDomainReady ||
            selected->ConnectionState != DMAConnectionConnected) {
            WARNING("__CheckAttachmentDomainLocked: domain %p is not ready for device %u, state %u, connection %u",
                    (void*)selected, parameters->Device, (uint32_t)selected->State,
                    (uint32_t)selected->ConnectionState);
            return OS_EBUSY;
        }

        for (uint32_t i = 0; i < selected->DeviceCount; i++) {
            if (selected->Devices[i] == parameters->Device) {
                status = DMAAddressSpaceCheck(selected->AddressSpace);
                if (status != OS_EOK) {
                    WARNING("__CheckAttachmentDomainLocked: address space of domain %p is not usable for device %u: %u",
                            (void*)selected, parameters->Device, status);
                }
                return status;
            }
        }

        WARNING("__CheckAttachmentDomainLocked: device %u is not a member of domain %p", parameters->Device, (void*)selected);
        return OS_EINVALPARAMS;
    }
    foreach (node, &manager->Domains) {
        DMATranslatedDomain_t* domain = node->value;

        if (domain->RequesterGroup == parameters->RequesterGroup) {
            WARNING("__CheckAttachmentDomainLocked: direct device %u cannot use requester group %llu, domain %p holds it",
                    parameters->Device, (unsigned long long)parameters->RequesterGroup,
                    (void*)domain);
            return OS_EBUSY;
        }

        for (uint32_t i = 0; i < domain->DeviceCount; i++) {
            if (domain->Devices[i] == parameters->Device) {
                WARNING("__CheckAttachmentDomainLocked: direct device %u is already listed in domain %p",
                        parameters->Device, (void*)domain);
                return OS_EBUSY;
            }
        }
    }
    return OS_EOK;
}

// Hold manager->Lock and UpdateLock. Publishing an attachment can merge
// recovery groups, putting direct devices and domain devices that share a
// requester or reset key into one group. Check the address space of every
// involved domain right before swapping groups so a healthy group never joins
// a failed unit unnoticed.
//
// Removal checks the recovery group's gate instead of domain health, so a
// domain on a failed unit can still lose its attachments and be destroyed once
// cleanup finishes.
static oserr_t
__CheckAttachmentPublicationLocked(
    _In_ DMAAttachmentRegistration_t* prepared)
{
    DMARecoveryUpdate_t* update = prepared->RecoveryUpdate;
    oserr_t              status;

    for (uint32_t i = 0; update != NULL && i < update->Count; i++) {
        DMATranslatedDomain_t* domain = update->Entries[i].Attachment->Domain;

        if (domain != NULL) {
            status = DMAAddressSpaceCheck(domain->AddressSpace);
            if (status != OS_EOK) {
                WARNING("__CheckAttachmentPublicationLocked: address space of domain %p is not usable for a group merge: %u",
                        (void*)domain, status);
                return status;
            }
        }
    }

    return __CheckAttachmentDomainLocked(
        prepared->Manager,
        prepared->Attachment->Domain,
        &prepared->Attachment->Configuration
    );
}

// Call without manager->Lock held (UpdateLock may be held). Scans the
// manager's domains and blocks and cancels any recovery group tied to a failed
// IOMMU unit. Errors are only logged so the caller still receives the result of
// its own attachment step. Blocked groups and queued events stay in place so a
// later call can retry.
static void
__BlockFailedUnitGroupsForAttachment(
    _In_ DMAManager_t* manager)
{
    oserr_t status = DMARecoveryBlockFailedUnits(manager);

    if (status != OS_EOK) {
        ERROR("__BlockFailedUnitGroupsForAttachment: failure propagation for manager %u failed: %u", manager->ID, status);
    }
}

// Hold manager->Lock and UpdateLock. Retrying an identical setup needs neither
// a free registry slot nor a new attachment ID. The domain is NULL for a direct
// attachment. A retry must name the same domain as the existing attachment.
static oserr_t
__PrepareAttachmentRequestLocked(
    _In_    DMAManager_t*                      manager,
    _In_    DMATranslatedDomain_t*             domain,
    _In_    const OSDmaAttachmentParameters_t* parameters,
    _InOut_ DMAAttachmentRegistration_t*       registration,
    _Out_   uint64_t*                          attachmentIDOut)
{
    DMAAttachment_t* existing;
    oserr_t          status;

    status = __CheckManagerStateLocked(manager);
    if (status != OS_EOK) {
        WARNING("__PrepareAttachmentRequestLocked: manager %u is not accepting attachments for device %u: %u",
                manager->ID, parameters->Device, status);
        return status;
    }

    status = __CheckAttachmentDomainLocked(manager, domain, parameters);
    if (status != OS_EOK) {
        return status;
    }

    existing = __FindAttachmentByDevice(manager, parameters->Device);
    if (existing != NULL) {
        if (existing->Domain != domain ||
            !__SameAttachmentConfiguration(&existing->Configuration, parameters)) {
            WARNING("__PrepareAttachmentRequestLocked: device %u is already attached as attachment %llu with another setup",
                    parameters->Device, (unsigned long long)existing->ID);
            return OS_EBUSY;
        }

        registration->Attachment = existing;
        *attachmentIDOut = existing->ID;
        return OS_EOK;
    }

    if (manager->Registry.Count == DMA_MANAGER_MAX_ATTACHMENTS) {
        ERROR("__PrepareAttachmentRequestLocked: manager %u has no free attachment slots for device %u",
              manager->ID, parameters->Device);
        return OS_ENOTSUPPORTED;
    }

    if (manager->Registry.NextAttachmentID == UINT64_MAX) {
        ERROR("__PrepareAttachmentRequestLocked: manager %u has run out of attachment IDs", manager->ID);
        return OS_EOVERFLOW;
    }

    registration->CreatesAttachment = true;
    *attachmentIDOut = manager->Registry.NextAttachmentID++;
    return OS_EOK;
}

// Allocate a new attachment record and work out which recovery groups it will
// join or merge. Nothing is published yet, so cancelling the registration
// frees everything made here.
static oserr_t
__CreateAttachmentRecord(
    _In_    DMAManager_t*                      manager,
    _In_    DMATranslatedDomain_t*             domain,
    _In_    const OSDmaAttachmentParameters_t* parameters,
    _In_    uint64_t                           attachmentID,
    _InOut_ DMAAttachmentRegistration_t*       registration)
{
    DMAAttachment_t* attachment;
    oserr_t          status;

    attachment = kmalloc(sizeof(DMAAttachment_t));
    if (attachment == NULL) {
        ERROR("__CreateAttachmentRecord: failed to allocate attachment %llu for device %u",
              (unsigned long long)attachmentID, parameters->Device);
        return OS_EOOM;
    }

    memset(attachment, 0, sizeof(DMAAttachment_t));
    ELEMENT_INIT(&attachment->Link, 0, attachment);
    ELEMENT_INIT(&attachment->Event.Link, 0, attachment);
    attachment->ID = attachmentID;
    attachment->Configuration = *parameters;
    attachment->Domain = domain;
    registration->Attachment = attachment;

    status = DMARecoveryPrepareRegistration(
        manager,
        attachment,
        &registration->RecoveryUpdate
    );
    if (status != OS_EOK) {
        ERROR("__CreateAttachmentRecord: DMARecoveryPrepareRegistration for attachment %llu, device %u failed: %u",
              (unsigned long long)attachmentID, parameters->Device, status);
    }
    return status;
}

// Prepare a new or retried attachment, with or without a domain. The order of
// the steps matters:
// 1. Take UpdateLock, so only one attachment change runs at a time, then check
//    that the manager can still accept attachments.
// 2. Validate the parameters and allocate the registration. Nothing is visible
//    to other callers yet.
// 3. Under Lock, decide whether this is a retry of an existing attachment or a
//    new one, and reserve the attachment ID for a new one.
// 4. Outside Lock, allocate the attachment record and work out its recovery
//    group changes. This may allocate memory, which is why Lock is not held.
// 5. Under Lock again, check that the recovery groups and the domain are still
//    valid, because Lock was dropped in step 4.
// On success UpdateLock stays held until the caller publishes or cancels. On
// failure everything made here is freed and UpdateLock is released.
static oserr_t
__PrepareAttachment(
    _In_  DMAManager_t*                      manager,
    _In_  DMATranslatedDomain_t*             domain,
    _In_  const OSDmaAttachmentParameters_t* parameters,
    _Out_ DMAAttachmentRegistration_t**      registrationOut,
    _Out_ OSDmaAttachmentId_t*               attachmentOut)
{
    DMAAttachmentRegistration_t* registration;
    uint64_t                     attachmentID;
    oserr_t                      status;

    // The syscall already checked the handle, but the manager can close without
    // waiting for UpdateLock. Recheck the state before validating parameters.
    MutexLock(&manager->UpdateLock);
    MutexLock(&manager->Lock);
    status = __CheckManagerStateLocked(manager);
    MutexUnlock(&manager->Lock);
    if (status != OS_EOK) {
        WARNING("__PrepareAttachment: manager %u is not accepting attachments for device %u: %u",
                manager->ID, parameters->Device, status);
        goto unlock;
    }

    status = __ValidateAttachmentParameters(parameters, domain != NULL);
    if (status != OS_EOK) {
        goto unlock;
    }

    registration = kmalloc(sizeof(DMAAttachmentRegistration_t));
    if (registration == NULL) {
        ERROR("__PrepareAttachment: failed to allocate a registration for device %u in manager %u",
              parameters->Device, manager->ID);
        status = OS_EOOM;
        goto unlock;
    }

    memset(registration, 0, sizeof(DMAAttachmentRegistration_t));
    registration->Manager = manager;

    MutexLock(&manager->Lock);
    status = __PrepareAttachmentRequestLocked(
        manager,
        domain,
        parameters,
        registration,
        &attachmentID
    );
    MutexUnlock(&manager->Lock);
    if (status != OS_EOK) {
        goto cancel;
    }

    if (registration->CreatesAttachment) {
        status = __CreateAttachmentRecord(
            manager,
            domain,
            parameters,
            attachmentID,
            registration
        );
        if (status != OS_EOK) {
            goto cancel;
        }
    }

    MutexLock(&manager->Lock);
    status = __CheckRecoveryUpdateLocked(
        manager,
        registration->RecoveryUpdate
    );
    if (status != OS_EOK) {
        WARNING("__PrepareAttachment: recovery groups for device %u in manager %u changed during prepare: %u",
                parameters->Device, manager->ID, status);
        MutexUnlock(&manager->Lock);
        goto cancel;
    }

    status = __CheckAttachmentDomainLocked(manager, domain, parameters);
    MutexUnlock(&manager->Lock);
    if (status != OS_EOK) {
        goto cancel;
    }

    TRACE("__PrepareAttachment: attachment %llu prepared for device %u in manager %u",
          (unsigned long long)attachmentID, parameters->Device, manager->ID);
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
    __BlockFailedUnitGroupsForAttachment(manager);
    return status;
}

oserr_t
DMAManagerPrepareAttachment(
    _In_  DMAManager_t*                      manager,
    _In_  const OSDmaAttachmentParameters_t* parameters,
    _Out_ DMAAttachmentRegistration_t**      registrationOut,
    _Out_ OSDmaAttachmentId_t*               attachmentOut)
{
    return __PrepareAttachment(manager, NULL, parameters, registrationOut, attachmentOut);
}

oserr_t
DMAManagerPrepareTranslatedAttachment(
    _In_  DMAManager_t*                            manager,
    _In_  DMATranslatedDomain_t*                    domain,
    _In_  const DMATranslatedAttachmentParameters_t* parameters,
    _Out_ DMAAttachmentRegistration_t**             registrationOut,
    _Out_ OSDmaAttachmentId_t*                       attachmentOut)
{
    OSDmaAttachmentParameters_t configuration;

    if (manager == NULL || domain == NULL || parameters == NULL ||
        registrationOut == NULL || attachmentOut == NULL ||
        parameters->AddressLimit > domain->AddressLimit) {
        WARNING("DMAManagerPrepareTranslatedAttachment: invalid translated attachment request, manager %p domain %p parameters %p",
                (void*)manager, (void*)domain, (void*)parameters);
        return OS_EINVALPARAMS;
    }

    // The fields read here never change after the domain is created, and the
    // caller keeps the domain pointer valid. The domain state, its member count
    // and the health of its unit can change, so those are checked later inside
    // the transaction.
    configuration = (OSDmaAttachmentParameters_t) {
        .Version = DMA_ATTACHMENT_VERSION,
        .Device = parameters->Device,
        .Host = domain->Host,
        .RequesterGroup = domain->RequesterGroup,
        .ResetGroup = parameters->ResetGroup,
        .AddressLimit = parameters->AddressLimit,
        .CachePolicy = domain->CachePolicy
    };

    return __PrepareAttachment(
        manager,
        domain,
        &configuration,
        registrationOut,
        attachmentOut
    );
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
        __BlockFailedUnitGroupsForAttachment(manager);
        return status;
    }

    status = __CheckAttachmentPublicationLocked(prepared);
    if (status != OS_EOK) {
        ERROR("DMAManagerPublishAttachment: __CheckAttachmentPublicationLocked for manager %u failed: %u",
              manager->ID, status);
        MutexUnlock(&manager->Lock);
        __BlockFailedUnitGroupsForAttachment(manager);
        return status;
    }

    if (prepared->CreatesAttachment) {
        // The ID copied cleanly and the manager is still live. Swap the new
        // recovery groups and attachment into the registry together under Lock
        // so readers never see a half-updated state.
        DMARecoveryApplyUpdate(prepared->RecoveryUpdate);
        list_append(&manager->Registry.Attachments, &prepared->Attachment->Link);
        manager->Registry.Count++;
        // The member count blocks domain destruction while devices are attached.
        if (prepared->Attachment->Domain != NULL) {
            prepared->Attachment->Domain->Members++;
        }
    }
    MutexUnlock(&manager->Lock);

    TRACE("DMAManagerPublishAttachment: attachment %llu published in manager %u",
          (unsigned long long)prepared->Attachment->ID, manager->ID);
    DMARecoveryDiscardUpdate(&prepared->RecoveryUpdate);
    kfree(prepared);
    *registration = NULL;
    MutexUnlock(&manager->UpdateLock);
    __BlockFailedUnitGroupsForAttachment(manager);
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
    __BlockFailedUnitGroupsForAttachment(manager);
}

oserr_t
DMAManagerRemoveAttachment(
    _In_ DMAManager_t* manager,
    _In_ uint64_t      attachmentID)
{
    DMARecoveryUpdate_t* update = NULL;
    DMAAttachment_t*     removedAttachment;
    oserr_t              status;

    // Removal runs in three steps:
    // 1. Under UpdateLock and Lock, find the attachment while the manager is live.
    // 2. Drop Lock and compute the updated recovery groups (allocates memory).
    // 3. Re-acquire Lock, confirm no queued events, bindings, or active
    //    operations appeared during step 2, then swap the groups and unlink.
    if (attachmentID == 0) {
        WARNING("DMAManagerRemoveAttachment: invalid attachment ID 0 for manager %u", manager->ID);
        return OS_EINVALPARAMS;
    }

    MutexLock(&manager->UpdateLock);
    MutexLock(&manager->Lock);
    status = __CheckManagerStateLocked(manager);
    if (status != OS_EOK) {
        WARNING("DMAManagerRemoveAttachment: manager %u is not accepting requests: %u",
              manager->ID, status);
        goto unlock_registry;
    }

    removedAttachment = __FindAttachmentByID(manager, attachmentID);
    if (removedAttachment == NULL) {
        WARNING("DMAManagerRemoveAttachment: attachment %llu not found for manager %u",
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
    // Pairs with the increment made when the attachment was published.
    if (removedAttachment->Domain != NULL) {
        assert(removedAttachment->Domain->Members != 0);
        removedAttachment->Domain->Members--;
    }
    MutexUnlock(&manager->Lock);
    TRACE("DMAManagerRemoveAttachment: attachment %llu removed from manager %u",
          (unsigned long long)attachmentID, manager->ID);
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
