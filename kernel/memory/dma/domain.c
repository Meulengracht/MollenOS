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
 * Translated DMA domains. Validates domain configs, tracks reserved hardware
 * IDs and devices, handles domain creation and teardown, and propagates IOMMU
 * unit failures to affected recovery groups.
 */

#include <assert.h>
#include <debug.h>
#include <handles/global.h>
#include <heap.h>
#include <string.h>

#include "attachment_private.h"
#include "private.h"

// Validate the config without touching the manager. Both ID arrays must be
// non-empty and within DMA_MANAGER_MAX_ATTACHMENTS. Address limits must nest:
// the IOVA space bounds sit inside the domain limit, and the domain limit sits
// inside the IOMMU unit limit.
static oserr_t
__ValidateDomain(
    _In_ const DMAIOMMUUnit_t*              unit,
    _In_ const DMATranslatedDomainConfig_t* config)
{
    if (unit == NULL || config == NULL || config->Host == UUID_INVALID ||
        config->RequesterGroup == 0 || config->HardwareIDs == NULL ||
        config->Devices == NULL || config->HardwareIDCount == 0 || config->DeviceCount == 0) {
        ERROR("__ValidateDomain: invalid domain config %p, unit %p",
              (void*)config, (void*)unit);
        return OS_EINVALPARAMS;
    }

    if (config->HardwareIDCount > DMA_MANAGER_MAX_ATTACHMENTS ||
        config->DeviceCount > DMA_MANAGER_MAX_ATTACHMENTS) {
        ERROR("__ValidateDomain: too many members (hardware IDs %u, devices %u, max %u)",
              config->HardwareIDCount, config->DeviceCount, DMA_MANAGER_MAX_ATTACHMENTS);
        return OS_ENOTSUPPORTED;
    }

    if ((config->CachePolicy != SHMDeviceCacheCoherent &&
         config->CachePolicy != SHMDeviceCacheNonCoherent) ||
        config->AddressLimit > unit->Config.AddressLimit ||
        config->AddressSpace.Bounds.Last > config->AddressLimit) {
        ERROR("__ValidateDomain: invalid cache policy %u or address limits "
              "(domain 0x%llx, unit 0x%llx, last 0x%llx)",
              (unsigned int)config->CachePolicy,
              (unsigned long long)config->AddressLimit,
              (unsigned long long)unit->Config.AddressLimit,
              (unsigned long long)config->AddressSpace.Bounds.Last);
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

// Sort the hardware IDs in place with an insertion sort. The set holds at most
// DMA_MANAGER_MAX_ATTACHMENTS entries, so this is fast enough and needs no
// allocation.
static void
__SortHardwareIDs(
    _InOut_ uint32_t* hardwareIDs,
    _In_    uint32_t  count)
{
    for (uint32_t i = 1; i < count; i++) {
        uint32_t value = hardwareIDs[i];
        uint32_t j = i;

        while (j != 0 && hardwareIDs[j - 1] > value) {
            hardwareIDs[j] = hardwareIDs[j - 1];
            j--;
        }
        hardwareIDs[j] = value;
    }
}

// Sort the device IDs in place, the same way as __SortHardwareIDs.
static void
__SortDevices(
    _InOut_ uuid_t*  devices,
    _In_    uint32_t count)
{
    for (uint32_t i = 1; i < count; i++) {
        uuid_t   value = devices[i];
        uint32_t j = i;

        while (j != 0 && devices[j - 1] > value) {
            devices[j] = devices[j - 1];
            j--;
        }
        devices[j] = value;
    }
}

// Copy the sets into the domain and sort them, so the caller can free or
// reorder its own arrays after the call returns. Sorting also puts duplicates
// next to each other, which the checks at the end look for. Returns
// OS_EINVALPARAMS for a duplicate hardware ID, a duplicate device, or an invalid
// device ID.
static oserr_t
__CopyDomainSets(
    _In_    const DMATranslatedDomainConfig_t* config,
    _InOut_ DMATranslatedDomain_t*             domain)
{
    memcpy(domain->HardwareIDs, config->HardwareIDs, config->HardwareIDCount * sizeof(uint32_t));
    memcpy(domain->Devices, config->Devices, config->DeviceCount * sizeof(uuid_t));

    __SortHardwareIDs(domain->HardwareIDs, domain->HardwareIDCount);
    __SortDevices(domain->Devices, domain->DeviceCount);

    for (uint32_t i = 1; i < domain->HardwareIDCount; i++) {
        if (domain->HardwareIDs[i] == domain->HardwareIDs[i - 1]) {
            ERROR("__CopyDomainSets: duplicate hardware ID %u", domain->HardwareIDs[i]);
            return OS_EINVALPARAMS;
        }
    }

    for (uint32_t i = 0; i < domain->DeviceCount; i++) {
        if (domain->Devices[i] == UUID_INVALID ||
            (i != 0 && domain->Devices[i] == domain->Devices[i - 1])) {
            ERROR("__CopyDomainSets: invalid or duplicate device %u", domain->Devices[i]);
            return OS_EINVALPARAMS;
        }
    }
    return OS_EOK;
}

// Returns true if the registered domain shares a device with the new domain.
static bool
__SharesDevice(
    _In_ const DMATranslatedDomain_t* registered,
    _In_ const DMATranslatedDomain_t* added)
{
    for (uint32_t i = 0; i < registered->DeviceCount; i++) {
        for (uint32_t j = 0; j < added->DeviceCount; j++) {
            if (registered->Devices[i] == added->Devices[j]) {
                return true;
            }
        }
    }
    return false;
}

// Returns true if the registered domain shares a hardware ID with the new domain.
// The caller must check that both domains are on the same unit first.
static bool
__SharesHardwareID(
    _In_ const DMATranslatedDomain_t* registered,
    _In_ const DMATranslatedDomain_t* added)
{
    for (uint32_t i = 0; i < registered->HardwareIDCount; i++) {
        for (uint32_t j = 0; j < added->HardwareIDCount; j++) {
            if (registered->HardwareIDs[i] == added->HardwareIDs[j]) {
                return true;
            }
        }
    }
    return false;
}

// Hold manager->Lock and UpdateLock. Verify the new domain's RequesterGroup,
// device IDs, and hardware IDs are free. Device IDs and RequesterGroup are
// manager-wide, so they cannot overlap with any registered domain or direct
// attachment. Hardware IDs only have meaning on one IOMMU unit, so they only
// conflict with domains on the same unit.
//
// Closing domains still count here, because their IDs stay reserved until
// destruction finishes. The lists are small and registration happens rarely, so
// simple nested loops are fast enough. Returns OS_ECANCELLED if the manager is
// closed, OS_EBUSY on a conflict, or OS_ENOTSUPPORTED if the domain list is full.
static oserr_t
__CheckDomainConflictsLocked(
    _In_ DMAManager_t*          manager,
    _In_ DMATranslatedDomain_t* added)
{
    if (manager->State != DMAManagerLive) {
        return OS_ECANCELLED;
    }

    foreach (node, &manager->Domains) {
        DMATranslatedDomain_t* domain = node->value;

        if (domain->RequesterGroup == added->RequesterGroup ||
            __SharesDevice(domain, added)) {
            return OS_EBUSY;
        }

        if (domain->Unit == added->Unit && __SharesHardwareID(domain, added)) {
            return OS_EBUSY;
        }
    }

    foreach (node, &manager->Registry.Attachments) {
        DMAAttachment_t* attachment = node->value;

        if (attachment->Configuration.RequesterGroup == added->RequesterGroup) {
            return OS_EBUSY;
        }

        for (uint32_t i = 0; i < added->DeviceCount; i++) {
            if (attachment->Configuration.Device == added->Devices[i]) {
                return OS_EBUSY;
            }
        }
    }

    if (manager->DomainCount == DMA_MANAGER_MAX_ATTACHMENTS) {
        return OS_ENOTSUPPORTED;
    }
    return OS_EOK;
}

// Allocate a domain and copy the config into it. The hardware ID and device sets
// are stored after the domain struct in one allocation. No manager reference is
// taken and nothing is registered. Returns OS_EOVERFLOW if the total size does
// not fit, OS_EOOM, or OS_EINVALPARAMS from __CopyDomainSets.
static oserr_t
__AllocateDomain(
    _In_  DMAManager_t*                      manager,
    _In_  DMAIOMMUUnit_t*                    unit,
    _In_  const DMATranslatedDomainConfig_t* config,
    _Out_ DMATranslatedDomain_t**            domainOut)
{
    DMATranslatedDomain_t* domain;
    size_t                 bytes;
    size_t                 hardwareIDBytes;
    oserr_t                status;

    // Check the total size for overflow before allocating.
    hardwareIDBytes = (size_t)config->HardwareIDCount * sizeof(uint32_t);
    if (hardwareIDBytes > SIZE_MAX - sizeof(DMATranslatedDomain_t) ||
        config->DeviceCount > (SIZE_MAX - sizeof(DMATranslatedDomain_t) - hardwareIDBytes) / sizeof(uuid_t)) {
        ERROR("__AllocateDomain: member sets overflow (hardware IDs %u, devices %u)",
              config->HardwareIDCount, config->DeviceCount);
        return OS_EOVERFLOW;
    }

    bytes = sizeof(DMATranslatedDomain_t) + hardwareIDBytes + (size_t)config->DeviceCount * sizeof(uuid_t);
    domain = kmalloc(bytes);
    if (domain == NULL) {
        ERROR("__AllocateDomain: failed to allocate %u bytes", (unsigned int)bytes);
        return OS_EOOM;
    }

    memset(domain, 0, bytes);
    ELEMENT_INIT(&domain->Link, 0, domain);
    domain->Manager = manager;
    domain->Unit = unit;
    domain->State = DMATranslatedDomainReady;
    domain->Host = config->Host;
    domain->RequesterGroup = config->RequesterGroup;
    domain->CachePolicy = config->CachePolicy;
    domain->AddressLimit = config->AddressLimit;
    domain->HardwareIDCount = config->HardwareIDCount;
    domain->DeviceCount = config->DeviceCount;

    // The hardware ID set comes first, followed by the device set.
    domain->HardwareIDs = (uint32_t*)(domain + 1);
    domain->Devices = (uuid_t*)(domain->HardwareIDs + domain->HardwareIDCount);

    status = __CopyDomainSets(config, domain);
    if (status != OS_EOK) {
        kfree(domain);
        return status;
    }

    *domainOut = domain;
    return OS_EOK;
}

// Add the domain to the manager's list. The conflict check, the unit health
// check and the insert all happen under the same locks. The failure pass walks
// the domain list under these locks too. Checking the unit health last, then
// adding the domain with no step that can fail, means a unit failure is either
// caught by this check or found by the failure pass through the new entry.
static oserr_t
__RegisterDomain(
    _In_ DMAManager_t*          manager,
    _In_ DMATranslatedDomain_t* domain)
{
    oserr_t status;

    MutexLock(&manager->UpdateLock);
    MutexLock(&manager->Lock);
    status = __CheckDomainConflictsLocked(manager, domain);
    if (status != OS_EOK) {
        WARNING("__RegisterDomain: domain for host %u conflicts or manager is closed: %u",
                domain->Host, status);
        goto unlock;
    }

    status = DMAAddressSpaceCheck(domain->AddressSpace);
    if (status != OS_EOK) {
        WARNING("__RegisterDomain: address space of host %u is not usable: %u",
                domain->Host, status);
        goto unlock;
    }

    list_append(&manager->Domains, &domain->Link);
    manager->DomainCount++;

unlock:
    MutexUnlock(&manager->Lock);
    MutexUnlock(&manager->UpdateLock);
    return status;
}

// Undo a domain that was never registered. The failure may come from a failed
// unit, so run the failure pass to stop the other domains on that unit as well.
static void
__DiscardUnregisteredDomain(
    _In_ DMAManager_t*          manager,
    _In_ DMATranslatedDomain_t* domain,
    _In_ oserr_t                originalStatus)
{
    oserr_t cleanupStatus;
    oserr_t propagationStatus;

    cleanupStatus = DMAConnectionDestroy(domain);
    if (cleanupStatus != OS_EOK) {
        ERROR("__DiscardUnregisteredDomain: hardware ID cleanup failed: %u (original %u)",
              cleanupStatus, originalStatus);
        goto propagate;
    }

    cleanupStatus = DMAAddressSpaceDestroy(&domain->AddressSpace);
    if (cleanupStatus != OS_EOK) {
        // The space was never published, so it has no mappings and cleanup
        // should not fail. If it does, its tracking state is inconsistent. Keep
        // the domain memory and the manager reference instead of freeing them,
        // so the problem can be inspected.
        ERROR("__DiscardUnregisteredDomain: unpublished space cleanup failed: %u (original %u)",
              cleanupStatus, originalStatus);
    }

propagate:
    propagationStatus = DMARecoveryBlockFailedUnits(manager);
    if (propagationStatus != OS_EOK) {
        ERROR("__DiscardUnregisteredDomain: failure propagation failed: %u (original %u)",
              propagationStatus, originalStatus);
    }

    if (cleanupStatus == OS_EOK) {
        kfree(domain);
        DMAManagerRelease(&manager);
    }
}

oserr_t
DMATranslatedDomainCreate(
    _In_    DMAManager_t*                      manager,
    _In_    DMAIOMMUUnit_t*                    unit,
    _In_    const DMATranslatedDomainConfig_t* config,
    _InOut_ DMATranslatedDomain_t**            domainOut)
{
    DMATranslatedDomain_t* domain;
    oserr_t                status;
    oserr_t                propagationStatus;

    // Creation order matters:
    // 1. Validate inputs and copy the sorted ID lists into a new domain record.
    // 2. Take the domain's own manager reference so the manager stays alive
    //    until the domain is destroyed, even if the caller drops its handle.
    // 3. Create the shared IOVA space and top-level IOMMU table.
    // 4. Reserve the hardware IDs on the backend before publishing.
    // 5. Register the domain under manager locks after checking conflicts and
    //    unit health, then check for any unit failure that happened mid-setup.
    if (manager == NULL || domainOut == NULL) {
        return OS_EINVALPARAMS;
    }

    if (*domainOut != NULL) {
        return OS_EBUSY;
    }

    status = __ValidateDomain(unit, config);
    if (status != OS_EOK) {
        return status;
    }

    status = __AllocateDomain(manager, unit, config, &domain);
    if (status != OS_EOK) {
        return status;
    }

    status = AcquireGlobalHandle(manager->ID, HandleTypeDMAManager, NULL);
    if (status != OS_EOK) {
        ERROR("DMATranslatedDomainCreate: failed to reference manager %u: %u", manager->ID, status);
        kfree(domain);
        return status;
    }

    status = DMAAddressSpaceCreateTranslated(
        unit,
        &config->AddressSpace,
        &domain->AddressSpace
    );
    if (status != OS_EOK) {
        ERROR("DMATranslatedDomainCreate: address space creation failed: %u", status);
        __DiscardUnregisteredDomain(manager, domain, status);
        return status;
    }

    status = DMAConnectionPrepare(domain);
    if (status != OS_EOK) {
        __DiscardUnregisteredDomain(manager, domain, status);
        return status;
    }

    status = __RegisterDomain(manager, domain);
    if (status != OS_EOK) {
        __DiscardUnregisteredDomain(manager, domain, status);
        return status;
    }

    *domainOut = domain;

    // Run the failure pass in case the unit failed right after the health check.
    // An error is only logged, because the domain is registered at this point.
    propagationStatus = DMARecoveryBlockFailedUnits(manager);
    if (propagationStatus != OS_EOK) {
        ERROR("DMATranslatedDomainCreate: failure propagation failed: %u", propagationStatus);
    }
    return OS_EOK;
}

oserr_t
DMATranslatedDomainDestroy(
    _InOut_ DMATranslatedDomain_t** domain)
{
    DMATranslatedDomain_t* released;
    DMAManager_t*          manager;
    oserr_t                status;
    oserr_t                propagationStatus;

    if (domain == NULL || *domain == NULL) {
        return OS_EOK;
    }

    released = *domain;

    manager = released->Manager;
    MutexLock(&manager->UpdateLock);
    MutexLock(&manager->Lock);
    if (released->Members != 0) {
        WARNING("DMATranslatedDomainDestroy: host %u still has %u members",
                released->Host, released->Members);
        MutexUnlock(&manager->Lock);
        MutexUnlock(&manager->UpdateLock);
        return OS_EBUSY;
    }

    // UpdateLock keeps out an attachment that is being prepared. Closing stops
    // new attachments from joining. It also makes the failure pass skip this
    // domain, so the pass never reads the address space pointer while it is
    // destroyed below, outside the locks.
    released->State = DMATranslatedDomainClosing;
    MutexUnlock(&manager->Lock);
    MutexUnlock(&manager->UpdateLock);

    status = DMAConnectionDestroy(released);
    if (status != OS_EOK) {
        ERROR("DMATranslatedDomainDestroy: hardware ID cleanup failed: %u", status);
        goto propagate;
    }

    status = DMAAddressSpaceDestroy(&released->AddressSpace);
    if (status != OS_EOK) {
        ERROR("DMATranslatedDomainDestroy: space cleanup failed: %u", status);
    }

    // Run the failure pass even when cleanup failed, so a failed unit is still
    // reported. If cleanup failed, the domain stays Closing and registered.
propagate:
    propagationStatus = DMARecoveryBlockFailedUnits(manager);
    if (propagationStatus != OS_EOK) {
        ERROR("DMATranslatedDomainDestroy: failure propagation failed: %u", propagationStatus);
    }
    if (status != OS_EOK) {
        return status;
    }

    // Hardware and space cleanup finished. Re-acquire the manager locks to
    // unlink the domain and release its manager reference.
    MutexLock(&manager->UpdateLock);
    MutexLock(&manager->Lock);
    list_remove(&manager->Domains, &released->Link);
    assert(manager->DomainCount != 0);
    manager->DomainCount--;
    MutexUnlock(&manager->Lock);
    MutexUnlock(&manager->UpdateLock);
    
    kfree(released);
    *domain = NULL;
    DMAManagerRelease(&manager);
    return OS_EOK;
}

oserr_t
DMATranslatedDomainFail(
    _In_ DMATranslatedDomain_t* domain,
    _In_ oserr_t                reason)
{
    oserr_t status;

    if (domain == NULL || reason == OS_EOK) {
        ERROR("DMATranslatedDomainFail: invalid domain %p or reason %u", (void*)domain, reason);
        return OS_EINVALPARAMS;
    }

    // Failing the unit affects every domain on it. The failure pass finds
    // them through the manager and stops the devices that depend on them.
    DMAIOMMUUnitFail(domain->Unit, reason);
    status = DMARecoveryBlockFailedUnits(domain->Manager);
    if (status != OS_EOK) {
        ERROR("DMATranslatedDomainFail: failure propagation failed: %u (reason %u)", status, reason);
    }
    return status;
}
