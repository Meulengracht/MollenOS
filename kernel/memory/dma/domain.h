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

#ifndef __DMA_DOMAIN_H__
#define __DMA_DOMAIN_H__

#include <dma/attachment.h>
#include "backend.h"

// Opaque declarations
DECL_STRUCT(DMATranslatedDomain);

/**
 * @brief Describes a translated domain, a group of devices that share one IOMMU address space.
 *
 * Devices use IOVAs (I/O virtual addresses) for DMA. The IOMMU translates
 * these into physical memory addresses. All devices in a domain see the same
 * IOVA space, so one allocator hands out addresses for all of them.
 *
 * Requesters lists the hardware identities the IOMMU sees on DMA requests from
 * these devices, for example the PCI address of each device. A requester ID
 * only has meaning within one IOMMU unit, and zero is a valid ID. Devices
 * lists the device IDs from the device manager registry. Both lists are copied
 * and may be in any order, but neither may contain duplicates. Host is the
 * device manager ID of the host device that the members sit behind.
 *
 * The manager keeps the device IDs and RequesterGroup reserved until the
 * domain is destroyed, so no other domain or direct attachment can use them.
 * The caller must work out which requesters and devices really belong together
 * before registering. Registration only checks the lists it is given.
 *
 * All members use the same CachePolicy and must be bound by the same
 * owner security ID (process security identity), which the first bound member
 * sets.
 * AddressLimit is the highest device address members can reach. Bounds.Last of
 * the address space cannot be above AddressLimit, and AddressLimit cannot be
 * above the limit of the IOMMU unit.
 */
typedef struct DMATranslatedDomainConfig {
    uuid_t                    Host;
    uint64_t                  RequesterGroup;
    enum SHMDeviceCachePolicy CachePolicy;
    uint64_t                  AddressLimit;
    const uint32_t*           Requesters;
    uint32_t                  RequesterCount;
    const uuid_t*             Devices;
    uint32_t                  DeviceCount;
    DMAIOVAConfig_t           AddressSpace;
} DMATranslatedDomainConfig_t;

/**
 * @brief Describes one device that joins a domain.
 *
 * Device must be listed in the domain. ResetGroup is a nonzero key shared by
 * devices that can only be reset together. AddressLimit is the highest device
 * address this device can reach and cannot be above the domain's AddressLimit.
 */
typedef struct DMATranslatedAttachmentParameters {
    uuid_t   Device;
    uint64_t ResetGroup;
    uint64_t AddressLimit;
} DMATranslatedAttachmentParameters_t;

/**
 * @brief Register a domain and create the address space its devices will share.
 *
 * The caller must hold a manager reference. The unit and config must stay
 * valid until this returns. The sets and the IOVA configuration are copied.
 * Each set, and the manager's list of domains, can hold at most
 * DMA_MANAGER_MAX_ATTACHMENTS entries. This does not program any hardware, so
 * no device is routed to the address space yet. The domain holds its own
 * manager reference until it is destroyed.
 * @param domainOut Must point to a NULL pointer. Left unchanged on failure.
 * @return OS_EOK, OS_EINVALPARAMS for malformed input, OS_EBUSY if an ID is
 *         already reserved or domainOut is not NULL, OS_ENOTSUPPORTED if a set
 *         or the domain list is full, OS_EOVERFLOW if a size or reference count
 *         overflows, OS_ECANCELLED if the manager is closed or the unit has
 *         failed, or an allocation or backend error.
 */
KERNELABI oserr_t KERNELAPI
DMATranslatedDomainCreate(
    _In_    DMAManager_t*                      manager,
    _In_    DMAIOMMUUnit_t*                    unit,
    _In_    const DMATranslatedDomainConfig_t* config,
    _InOut_ DMATranslatedDomain_t**            domainOut);

/**
 * @brief Destroy the address space of an unused domain and unregister the domain.
 *
 * No other thread may still be using this domain pointer. If attachments still
 * belong to the domain, or something still references the address space (for
 * example an open DMA context), this returns OS_EBUSY. Once destruction starts
 * the domain is marked Closing. A Closing domain keeps its IDs reserved and
 * accepts no new attachments until cleanup succeeds. If cleanup fails, the
 * pointer stays valid so the call can be retried. This does not disconnect any
 * hardware requester.
 * @param domain Cleared on success. NULL or *domain == NULL is a no-op.
 * @return OS_EOK, OS_EBUSY while attachments or address space references
 *         remain, or an error from the address space cleanup.
 */
KERNELABI oserr_t KERNELAPI
DMATranslatedDomainDestroy(
    _InOut_ DMATranslatedDomain_t** domain);

/**
 * @brief Prepare the attachment of one device that is listed in the domain.
 *
 * This works like DMAManagerPrepareAttachment. The caller must keep the
 * manager reference and the domain valid until the result is published or
 * cancelled. Host, RequesterGroup and CachePolicy are taken from the domain.
 * No address ranges are needed because the device uses the domain's address
 * space. A retry with identical parameters returns the same attachment ID.
 * On success the update lock stays held, so finish with
 * DMAManagerPublishAttachment or DMAManagerCancelAttachment. The rules
 * described for those two functions apply here too.
 * @return OS_EOK, OS_EINVALPARAMS for NULL arguments, a device that is not in
 *         the domain or an AddressLimit above the domain's, OS_EBUSY if the
 *         domain is Closing, or any error DMAManagerPrepareAttachment gives.
 */
KERNELABI oserr_t KERNELAPI
DMAManagerPrepareTranslatedAttachment(
    _In_  DMAManager_t*                              manager,
    _In_  DMATranslatedDomain_t*                     domain,
    _In_  const DMATranslatedAttachmentParameters_t* parameters,
    _Out_ DMAAttachmentRegistration_t**              registrationOut,
    _Out_ OSDmaAttachmentId_t*                       attachmentOut);

/**
 * @brief Mark the IOMMU unit of a domain as failed and stop all devices that depend on it.
 *
 * The failure is permanent. Every domain on the same unit is affected. The
 * recovery groups holding those devices are blocked so no new DMA starts, and
 * their open DMA contexts are cancelled. Call this from thread context with a
 * live domain, and with no backend, manager, group or SHM lock held. A second
 * call retries the cancellation and keeps the first failure reason. Cancelling
 * in software does not prove that the hardware stopped.
 * @return OS_EINVALPARAMS for a NULL domain or an OS_EOK reason. Otherwise the
 *         result of the cancellation. If it fails, the groups stay blocked.
 */
KERNELABI oserr_t KERNELAPI
DMATranslatedDomainFail(
    _In_ DMATranslatedDomain_t* domain,
    _In_ oserr_t                reason);

#endif //!__DMA_DOMAIN_H__
