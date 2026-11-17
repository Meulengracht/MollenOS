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
 * ARM SMMU Discovery Interface
 * Builds a lookup table of SMMU controllers and device DMA routes from
 * ACPI IORT or a parsed Device Tree.
 */

#ifndef __ARM_SMMU_DISCOVERY_H__
#define __ARM_SMMU_DISCOVERY_H__

#include <os/osdefs.h>
#include <os/types/dma.h>
#include <os/types/dma_attachment.h>
#include <devicetree.h>

#include "description.h"

// Opaque data type declarations
DECL_STRUCT(ArmIortCatalogue);
DECL_STRUCT(ArmSMMUDiscovery);

/**
 * @brief Firmware route connecting one device to an SMMU controller.
 *
 * Devices perform DMA (direct memory access) by reading and writing RAM on
 * their own. Every DMA request carries a numeric requester ID (also called a
 * stream ID) so the SMMU can tell which device sent it. For a PCI device, that
 * ID starts as its 16-bit bus, device, and function number, though bridges or
 * firmware tables may remap it before it reaches the SMMU.
 *
 * UnitIndex picks which SMMU in the ArmSMMUDiscovery_t handles this device.
 * Requesters holds all incoming SMMU IDs the device can present, sorted and
 * free of duplicates, up to DMA_MANAGER_MAX_ATTACHMENTS entries. A device can
 * have multiple IDs when firmware lists several entries or when an MMU-500
 * hardware bitmask matches a power-of-two group of IDs. CachePolicy tells
 * whether device DMA stays coherent with CPU caches, and AddressLimit is the
 * highest physical address the device can reach.
 *
 * A successful lookup only gives the firmware mapping. Before putting the
 * device into a translation domain, the caller must still check bus address
 * windows, other functions on the same PCI device, and any neighboring devices
 * that share the same requester ID.
 */
typedef struct ArmSMMUDeviceRoute {
    uint32_t                  UnitIndex;
    uint32_t                  RequesterCount;
    uint32_t                  Requesters[DMA_MANAGER_MAX_ATTACHMENTS];
    enum SHMDeviceCachePolicy CachePolicy;
    uint64_t                  AddressLimit;
} ArmSMMUDeviceRoute_t;

/**
 * @brief Builds an SMMU discovery table from a validated ACPI IORT catalogue.
 *
 * Copies unit descriptions and device routes into private storage without
 * reading ACPI again or touching hardware registers. The caller may free the
 * IORT catalogue as soon as this call returns.
 *
 * @param catalogue    Checked IORT catalogue from ArmIortCreate.
 * @param discoveryOut Must point to NULL. Receives the new discovery table on success.
 * @return OS_EOK on success, OS_EINVALPARAMS for NULL arguments, OS_EBUSY if
 *         *discoveryOut already holds a pointer, OS_ENOTSUPPORTED if the table
 *         exceeds static discovery limits, or OS_EOOM on allocation failure.
 *         Failure leaves *discoveryOut untouched and frees partial storage.
 */
KERNELABI oserr_t KERNELAPI
ArmSMMUDiscoverIort(
    _In_    const ArmIortCatalogue_t* catalogue,
    _InOut_ ArmSMMUDiscovery_t**      discoveryOut);

/**
 * @brief Builds an SMMU discovery table from a parsed Device Tree.
 *
 * Reads SMMU descriptions and device routes from tree without touching MMIO
 * registers or configuring hardware. Keep tree alive and unchanged while any
 * discovery lookups run.
 *
 * @param tree         Parsed Device Tree from DeviceTreeCreate.
 * @param discoveryOut Must point to NULL. Receives the new discovery table on success.
 * @return Status codes documented for ArmSMMUDiscoverIort. A valid tree with
 *         no translated routes succeeds and returns an empty discovery table.
 */
KERNELABI oserr_t KERNELAPI
ArmSMMUDiscoverDeviceTree(
    _In_    const DeviceTree_t*  tree,
    _InOut_ ArmSMMUDiscovery_t** discoveryOut);

/**
 * @brief Returns the number of supported SMMU controllers found, or 0 for NULL.
 *
 * @param discovery Discovery table from ArmSMMUDiscoverIort or ArmSMMUDiscoverDeviceTree.
 */
KERNELABI uint32_t KERNELAPI
ArmSMMUGetUnitCount(
    _In_ const ArmSMMUDiscovery_t* discovery);

/**
 * @brief Copies the firmware description of one SMMU controller.
 *
 * Keep discovery alive during this call.
 *
 * @param discovery      Active discovery table.
 * @param unitIndex      Zero-based controller index below ArmSMMUGetUnitCount.
 * @param descriptionOut Receives the copied description on success.
 * @return OS_EOK on success, OS_EINVALPARAMS for NULL arguments, or OS_ENOENT
 *         when unitIndex is out of range. Failure leaves descriptionOut unchanged.
 */
KERNELABI oserr_t KERNELAPI
ArmSMMUGetUnitDescription(
    _In_  const ArmSMMUDiscovery_t* discovery,
    _In_  uint32_t                  unitIndex,
    _Out_ ArmSMMUDescription_t*      descriptionOut);

/**
 * @brief Looks up the SMMU route for one PCI device.
 *
 * Pass the PCI segment group number and the device's 16-bit bus, device, and
 * function requester ID. Keep discovery alive during the lookup. If firmware
 * routes the device through an unsupported SMMU or feature, this fails with an
 * error so the caller never bypasses translation for direct RAM access.
 *
 * @param discovery   Active discovery table.
 * @param segment     PCI segment group number.
 * @param requesterId 16-bit PCI requester ID seen at the host bridge.
 * @param routeOut    Receives the resolved SMMU route on success.
 * @return OS_EOK on success, OS_ENOENT when no firmware rule covers the device,
 *         OS_ENOTSUPPORTED when the route needs an unsupported feature, or
 *         OS_EINVALPARAMS for NULL arguments. Failure leaves routeOut unchanged.
 */
KERNELABI oserr_t KERNELAPI
ArmSMMUResolvePci(
    _In_  const ArmSMMUDiscovery_t* discovery,
    _In_  uint32_t                  segment,
    _In_  uint16_t                  requesterId,
    _Out_ ArmSMMUDeviceRoute_t*      routeOut);

/**
 * @brief Looks up every SMMU requester ID for a named non-PCI ACPI device.
 *
 * absolutePath must be a null-terminated ACPI namespace path starting with
 * '\\' (for example "\\_SB.ETH0") and must match the firmware node name
 * exactly. Keep discovery alive during the lookup. All of the device's IDs are
 * returned together, never a partial list.
 *
 * @param discovery    Active discovery table.
 * @param absolutePath Exact absolute ACPI path of the device.
 * @param routeOut     Receives the resolved SMMU route on success.
 * @return Same statuses as ArmSMMUResolvePci, plus OS_EINVALPARAMS when
 *         absolutePath does not start with '\\'. Targeting multiple SMMUs or
 *         exceeding DMA_MANAGER_MAX_ATTACHMENTS IDs returns OS_ENOTSUPPORTED.
 */
KERNELABI oserr_t KERNELAPI
ArmSMMUResolveAcpiDevice(
    _In_  const ArmSMMUDiscovery_t* discovery,
    _In_  const char*               absolutePath,
    _Out_ ArmSMMUDeviceRoute_t*      routeOut);

/**
 * @brief Looks up the complete iommus list for a Device Tree platform device.
 *
 * Reads every entry in the device node's iommus property as one set. device must
 * belong to the live DeviceTree_t passed to ArmSMMUDiscoverDeviceTree. PCI
 * functions use ArmSMMUResolvePci because PCI devices are mapped by ID range
 * on their host bridge (iommu-map).
 *
 * @param discovery Active discovery table built from a Device Tree.
 * @param device    Node pointer from the original DeviceTree_t.
 * @param routeOut  Receives the resolved SMMU route on success.
 * @return Same statuses as ArmSMMUResolveAcpiDevice, or OS_EINVALPARAMS when
 *         device is outside the original tree. Failure leaves routeOut unchanged.
 */
KERNELABI oserr_t KERNELAPI
ArmSMMUResolveDeviceTreeDevice(
    _In_  const ArmSMMUDiscovery_t* discovery,
    _In_  const DeviceTreeNode_t*    device,
    _Out_ ArmSMMUDeviceRoute_t*      routeOut);

/**
 * @brief Frees a discovery table and clears *discovery to NULL.
 *
 * Call this only after all users of the table have finished. Passing NULL or a
 * pointer to NULL is safe. Unit descriptions hold plain data with no hardware
 * mappings to tear down.
 *
 * @param discovery Pointer to the discovery handle to release and clear.
 */
KERNELABI void KERNELAPI
ArmSMMUDiscoveryDestroy(
    _InOut_ ArmSMMUDiscovery_t** discovery);

#endif //!__ARM_SMMU_DISCOVERY_H__
