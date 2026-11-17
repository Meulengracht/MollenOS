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
 * Translated DMA domains. A domain groups devices behind an IOMMU so they
 * share one address space and can be created, connected, attached, and torn
 * down together.
 */

#ifndef __DMA_DOMAIN_H__
#define __DMA_DOMAIN_H__

#include <dma/attachment.h>
#include "backend.h"

// Opaque declarations
DECL_STRUCT(DMATranslatedDomain);

/**
 * @brief Settings for a translated domain: a set of devices sharing one IOMMU address space.
 *
 * Devices in a translated domain use device addresses (IOVAs) for DMA instead of
 * raw physical addresses. The IOMMU translates those IOVAs into physical RAM
 * addresses. All devices in one domain share a single IOVA space, so one
 * allocator hands out addresses for all of them.
 *
 * HardwareIDs lists the hardware tags the IOMMU sees on DMA requests from
 * these devices, such as a PCI bus, device, and function number. A hardware ID
 * only has meaning on one IOMMU unit, and zero is a valid ID. Devices lists the
 * device IDs from the device manager registry. Both lists are copied in any
 * order, and neither may hold duplicates.
 *
 * Host is the device manager ID of the parent controller or host bridge these
 * devices sit behind. RequesterGroup is a nonzero key for devices that share a
 * hardware bus path and cannot be isolated from one another. The manager keeps
 * the Devices list and RequesterGroup reserved until the domain is destroyed,
 * preventing any other domain or direct attachment from using them. The caller
 * must work out which hardware IDs and devices belong together before
 * calling DMATranslatedDomainCreate. Registration only checks the lists it
 * receives.
 *
 * All members must use the same CachePolicy and be bound by the same process
 * security identity (OwnerSecurityID), fixed by the first member that binds.
 * AddressLimit is the highest device address any member may reach.
 * AddressSpace.Bounds.Last cannot exceed AddressLimit, and AddressLimit cannot
 * exceed the IOMMU unit's limit.
 */
typedef struct DMATranslatedDomainConfig {
    uuid_t                    Host;
    uint64_t                  RequesterGroup;
    enum SHMDeviceCachePolicy CachePolicy;
    uint64_t                  AddressLimit;
    const uint32_t*           HardwareIDs;
    uint32_t                  HardwareIDCount;
    const uuid_t*             Devices;
    uint32_t                  DeviceCount;
    DMAIOVAConfig_t           AddressSpace;
} DMATranslatedDomainConfig_t;

/**
 * @brief Per-device settings when joining an existing translated domain.
 *
 * Device must already appear in the domain's Devices list. ResetGroup is a
 * nonzero key shared by devices that can only be reset together, so if one
 * stops or resets, the kernel stops the whole group. AddressLimit is the
 * highest device address this device can reach and cannot exceed the domain's
 * AddressLimit.
 */
typedef struct DMATranslatedAttachmentParameters {
    uuid_t   Device;
    uint64_t ResetGroup;
    uint64_t AddressLimit;
} DMATranslatedAttachmentParameters_t;

/**
 * @brief Registers a domain and allocates the shared IOMMU address space for its devices.
 *
 * The typical lifecycle is Create, Connect, PrepareTranslatedAttachment (plus
 * PublishAttachment), and Destroy once all attachments are removed.
 *
 * The caller must hold a manager reference, and keep unit and config valid
 * until this returns. Both ID lists and the IOVA settings are copied. Each list
 * and the manager's domain list hold at most DMA_MANAGER_MAX_ATTACHMENTS entries.
 *
 * Creating a domain writes no hardware routes yet. The backend reserves every
 * hardware ID across all managers on the unit, and the domain takes its own
 * manager reference until destroyed. Call DMATranslatedDomainConnect before
 * preparing any member attachment.
 *
 * @param manager   DMA manager that owns this domain.
 * @param unit      IOMMU unit that translates for these hardware IDs.
 * @param config    Domain devices, hardware IDs, limits, and IOVA settings.
 * @param domainOut Pointer to a NULL slot that receives the domain. Unchanged on failure.
 * @return OS_EOK on success, OS_EINVALPARAMS for invalid input, OS_EBUSY if an
 *         ID is already reserved or *domainOut is not NULL, OS_ENOTSUPPORTED if
 *         a list or the domain limit is exceeded, OS_EOVERFLOW if a size or
 *         reference count overflows, OS_ECANCELLED if the manager is closed or
 *         the unit has failed, or an allocation or backend error.
 */
KERNELABI oserr_t KERNELAPI
DMATranslatedDomainCreate(
    _In_    DMAManager_t*                      manager,
    _In_    DMAIOMMUUnit_t*                    unit,
    _In_    const DMATranslatedDomainConfig_t* config,
    _InOut_ DMATranslatedDomain_t**            domainOut);

/**
 * @brief Routes every reserved hardware ID in the domain to its IOMMU address space.
 *
 * Keep all devices in the domain idle with no DMA transfers running, and make
 * sure only one thread at a time calls Connect or Destroy on this domain
 * pointer. The domain must be Ready with no member attachments yet, and both its
 * manager and IOMMU unit must be healthy.
 *
 * The hardware callback runs without holding manager or recovery-group locks.
 * Afterward, the domain rechecks that the manager and unit are still healthy
 * before allowing attachments to join.
 *
 * Calling Connect again on a domain that is already Connected does nothing and
 * returns OS_EOK while the domain stays Ready and healthy, even after members
 * join. Never call Connect again after a failed attempt. Call Destroy instead,
 * because any partial hardware routes stay recorded until a clean disconnect
 * succeeds.
 *
 * @param domain Domain created by DMATranslatedDomainCreate.
 * @return OS_EOK on success, OS_EINVALPARAMS for NULL, OS_EBUSY if the domain
 *         state or membership does not allow connecting, OS_ECANCELLED if the
 *         manager closed or the unit failed, or a backend error. Failure never
 *         frees the domain or drops its hardware ID reservations.
 */
KERNELABI oserr_t KERNELAPI
DMATranslatedDomainConnect(
    _In_ DMATranslatedDomain_t* domain);

/**
 * @brief Tears down an unused domain's hardware connection, address space, and registration.
 *
 * No other thread may still use this domain pointer. If any attachment still
 * belongs to the domain, or if anything still holds a reference to its address
 * space (such as an open shared-memory DMA context), this returns OS_EBUSY.
 *
 * Once destruction starts, the domain is marked Closing so no new attachments
 * can join. The caller must stop all DMA on the domain's devices before calling
 * this. Hardware IDs are disconnected before the connection drops its reference
 * to the address space. If a hardware wait times out or the unit has failed, we
 * cannot confirm the hardware stopped reading the page tables, so the pointer
 * and reservations stay intact for a later retry.
 *
 * @param domain Address of the domain pointer. Cleared to NULL on success.
 *               Does nothing if domain or *domain is NULL.
 * @return OS_EOK on success, OS_EBUSY while attachments or address space
 *         references remain, or an error from hardware ID or space cleanup.
 */
KERNELABI oserr_t KERNELAPI
DMATranslatedDomainDestroy(
    _InOut_ DMATranslatedDomain_t** domain);

/**
 * @brief Prepares an attachment for one device listed in the domain.
 *
 * Works like DMAManagerPrepareAttachment, except Host, RequesterGroup, and
 * CachePolicy come from the domain, and no physical ranges are supplied because
 * the device uses the domain's shared IOVA space. Keep the manager reference
 * and the domain pointer valid until the result is published or cancelled.
 *
 * A retry with the same parameters returns the same attachment ID. On success,
 * the manager's UpdateLock stays held while the caller copies the ID back to
 * user space, so always finish with DMAManagerPublishAttachment or
 * DMAManagerCancelAttachment.
 *
 * @param manager         DMA manager that owns the domain.
 * @param domain          Connected domain listing the device.
 * @param parameters      Device ID, reset group, and device address limit.
 * @param registrationOut Receives the pending registration handle on success.
 * @param attachmentOut   Receives the attachment ID on success.
 * @return OS_EOK on success, OS_EINVALPARAMS for NULL inputs, a device outside
 *         the domain, or an AddressLimit above the domain's limit, OS_EBUSY if
 *         the domain is Closing or not Connected, or any error from
 *         DMAManagerPrepareAttachment.
 */
KERNELABI oserr_t KERNELAPI
DMAManagerPrepareTranslatedAttachment(
    _In_  DMAManager_t*                              manager,
    _In_  DMATranslatedDomain_t*                     domain,
    _In_  const DMATranslatedAttachmentParameters_t* parameters,
    _Out_ DMAAttachmentRegistration_t**              registrationOut,
    _Out_ OSDmaAttachmentId_t*                       attachmentOut);

/**
 * @brief Marks the domain's IOMMU unit as failed and stops every dependent device.
 *
 * Unit failure is permanent and affects every domain on the same IOMMU unit.
 * Recovery groups holding those devices are blocked so no new DMA can start,
 * and open shared-memory DMA contexts are cancelled.
 *
 * Call from thread context with a live domain and no backend, manager, group,
 * or SHM lock held. Calling this again keeps the first failure reason and
 * retries cancellation. Software cancellation stops new requests, though the
 * caller must still reset or stop physical hardware separately.
 *
 * @param domain Live domain whose IOMMU unit failed.
 * @param reason Non-OS_EOK error code describing the failure.
 * @return OS_EINVALPARAMS for a NULL domain or OS_EOK reason. Otherwise returns
 *         the result of group cancellation (groups stay blocked on error).
 */
KERNELABI oserr_t KERNELAPI
DMATranslatedDomainFail(
    _In_ DMATranslatedDomain_t* domain,
    _In_ oserr_t                reason);

#endif //!__DMA_DOMAIN_H__
