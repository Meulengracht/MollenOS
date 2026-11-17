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
 * ARM SMMU Discovery Internal Types
 * Shared table structures populated by the ACPI IORT and Device Tree readers.
 */

#ifndef __ARM_SMMU_DISCOVERY_PRIVATE_H__
#define __ARM_SMMU_DISCOVERY_PRIVATE_H__

#include "discovery.h"

// Fixed caps on units, device sources, and mapping ranges. Keeping these
// arrays inline bounds memory use on malformed firmware tables and lets failed
// discovery clean up in one kfree.
#define ARM_SMMU_MAX_UNITS  8U
#define ARM_SMMU_MAX_NODES  256U
#define ARM_SMMU_MAX_RANGES 256U

/**
 * @brief One ID translation rule from a device source toward a hardware target.
 *
 * Incoming IDs in the inclusive span [InputFirst, InputLast] translate to
 * OutputFirst + (input - InputFirst) on the target unit. When MapsToOneId is
 * true, every matching input collapses to OutputFirst.
 *
 * On an MMU-500, stream matching registers can ignore bits set in
 * IgnoredBitsMask when comparing an incoming ID against OutputFirst. Each
 * ignored bit can be 0 or 1, so one hardware rule matches a power-of-two
 * group of IDs. Discovery expands those combinations into exact IDs during
 * lookup so generic DMA code never has to parse MMU-500 masks.
 *
 * Status records what the target is: OS_EOK for a supported SMMU,
 * OS_ENOENT for an interrupt-only ITS controller (no DMA translation), or an
 * error such as OS_ENOTSUPPORTED when the target cannot be used.
 */
typedef struct ArmSMMURouteRange {
    uint32_t InputFirst;
    uint32_t InputLast;
    uint32_t OutputFirst;
    uint32_t IgnoredBitsMask;
    uint32_t UnitIndex;
    bool     MapsToOneId;
    oserr_t  Status;
} ArmSMMURouteRange_t;

/**
 * @brief One DMA source (a PCI host bridge or a named platform device).
 *
 * FirstRange and RangeCount select this source's slice inside the shared
 * ArmSMMUDiscovery.Ranges array. Named devices identify themselves with either
 * a heap-copied AcpiPath or a DeviceTreeDevice pointer into the live tree. PCI
 * host bridges leave both NULL and match by Segment.
 *
 * For Device Tree PCI hosts, FirstRequesterId and LastRequesterId hold the
 * valid bus-range bounds checked before RequesterIdMask (from iommu-map-mask)
 * strips bits such as function numbers. If any bus property or mapping for this
 * source uses a feature the kernel cannot handle, Status records that error so
 * lookups block the device while leaving unrelated devices usable.
 */
typedef struct ArmSMMUSource {
    char*                     AcpiPath;
    const DeviceTreeNode_t*   DeviceTreeDevice;
    uint32_t                  Segment;
    uint32_t                  RequesterIdMask;
    uint16_t                  FirstRequesterId;
    uint16_t                  LastRequesterId;
    uint32_t                  FirstRange;
    uint32_t                  RangeCount;
    enum SHMDeviceCachePolicy CachePolicy;
    uint64_t                  AddressLimit;
    oserr_t                   Status;
} ArmSMMUSource_t;

/**
 * @brief Span of SMMU IDs reserved by firmware for pre-boot DMA.
 *
 * Populated from ACPI IORT Reserved Memory Region (RMR) nodes, which mark
 * devices such as active boot framebuffers that keep doing DMA across boot.
 * First and Last are inclusive IDs on UnitIndex. Any route matching one of
 * these IDs is refused so the kernel never breaks active firmware DMA, while
 * other IDs on the same unit continue to work.
 */
typedef struct ArmSMMUFirmwareHeldIds {
    uint32_t UnitIndex;
    uint32_t First;
    uint32_t Last;
} ArmSMMUFirmwareHeldIds_t;

/**
 * @brief Internal lookup table produced by ACPI IORT or Device Tree discovery.
 *
 * ACPI IORT discovery copies everything it needs so ACPICA can release the raw
 * firmware table right away. Device Tree discovery keeps a pointer to the
 * caller's live DeviceTree_t for node identity checks. Fixed-size arrays keep
 * memory bounded and allow cleanup without walking per-range allocations.
 */
struct ArmSMMUDiscovery {
    const DeviceTree_t*      DeviceTree;
    uint32_t                 UnitCount;
    uint32_t                 RouteCount;
    uint32_t                 RangeCount;
    uint32_t                 HeldIdCount;
    ArmSMMUDescription_t     Units[ARM_SMMU_MAX_UNITS];
    ArmSMMUSource_t          Routes[ARM_SMMU_MAX_NODES];
    ArmSMMURouteRange_t      Ranges[ARM_SMMU_MAX_RANGES];
    ArmSMMUFirmwareHeldIds_t HeldIds[ARM_SMMU_MAX_RANGES];
};

#endif //!__ARM_SMMU_DISCOVERY_PRIVATE_H__
