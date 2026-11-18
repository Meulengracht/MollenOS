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
 * Fixed-size lookup tables filled in by the ACPI IORT and Device Tree readers
 * and queried by discovery.c when resolving device DMA routes.
 */

#ifndef __ARM_SMMU_DISCOVERY_PRIVATE_H__
#define __ARM_SMMU_DISCOVERY_PRIVATE_H__

#include "discovery.h"

// Fixed caps on SMMU units, scanned firmware nodes, DMA sources, ID mapping
// rules, and firmware-reserved ID spans. Keeping arrays inline bounds RAM use
// on malformed firmware tables and lets cleanup free everything in one kfree.
// Each source comes from a single firmware node, so matching the source cap to
// the node cap ensures a valid device source is never dropped for lack of space.
#define ARM_SMMU_MAX_UNITS    8U
#define ARM_SMMU_MAX_NODES    256U
#define ARM_SMMU_MAX_SOURCES  ARM_SMMU_MAX_NODES
#define ARM_SMMU_MAX_RANGES   256U
#define ARM_SMMU_MAX_HELD_IDS 256U

/**
 * @brief One rule mapping a device's requester IDs onto an SMMU stream ID range.
 *
 * Input IDs in the inclusive range [InputFirst, InputLast] shift linearly to
 * OutputFirst + (input - InputFirst) on UnitIndex. When MapsToOneId is true,
 * every matching input maps to OutputFirst instead of adding an offset.
 *
 * On MMU-500 hardware, stream-matching registers can ignore any bit set to 1
 * in IgnoredBitsMask when comparing incoming IDs against OutputFirst. Because
 * each ignored bit can match either 0 or 1, one hardware rule covers a
 * power-of-two group of IDs. Discovery expands those combinations into exact
 * sorted IDs at lookup time so the rest of the DMA layer stays hardware-neutral.
 *
 * Status tells what the target actually is: OS_EOK for a usable SMMU,
 * OS_ENOENT for an interrupt-only ITS controller (MSI routing only), or an
 * error such as OS_ENOTSUPPORTED when the target SMMU cannot be used.
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
 * @brief One DMA-capable device source (a PCI host bridge or platform device).
 *
 * FirstRange and RangeCount select this source's slice inside Ranges. Named
 * platform devices match by either a heap-allocated AcpiPath string or a
 * DeviceTreeDevice pointer into the live Device Tree. PCI host bridges leave
 * both NULL and match by Segment instead.
 *
 * For Device Tree PCI hosts, FirstRequesterId and LastRequesterId hold the
 * bus-range bounds checked before RequesterIdMask (from iommu-map-mask) strips
 * bits such as PCI function numbers. If any bus window or mapping property on
 * this source needs an unsupported feature, Status records that error so
 * lookups fail cleanly for this device while unrelated devices keep working.
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
 * @brief Stream ID span reserved by firmware for ongoing pre-boot DMA.
 *
 * Filled from ACPI IORT Reserved Memory Region (RMR) nodes, which mark hardware
 * such as an active boot display framebuffer that continues DMA across OS
 * startup. First and Last are inclusive stream IDs on UnitIndex. Lookups reject
 * any route touching these IDs so the kernel never breaks active firmware DMA,
 * while other IDs on the same SMMU remain available.
 */
typedef struct ArmSMMUFirmwareHeldIds {
    uint32_t UnitIndex;
    uint32_t First;
    uint32_t Last;
} ArmSMMUFirmwareHeldIds_t;

/**
 * @brief Internal discovery table built from ACPI IORT or Device Tree.
 *
 * ACPI IORT discovery copies everything it needs up front so ACPICA can free
 * the raw firmware table right away. Device Tree discovery keeps a pointer to
 * the caller's live DeviceTree_t for pointer-based node checks. Fixed inline
 * arrays bound memory usage and keep cleanup simple.
 */
struct ArmSMMUDiscovery {
    const DeviceTree_t*      DeviceTree;
    uint32_t                 UnitCount;
    uint32_t                 SourceCount;
    uint32_t                 RangeCount;
    uint32_t                 HeldIdCount;
    ArmSMMUDescription_t     Units[ARM_SMMU_MAX_UNITS];
    ArmSMMUSource_t          Sources[ARM_SMMU_MAX_SOURCES];
    ArmSMMURouteRange_t      Ranges[ARM_SMMU_MAX_RANGES];
    ArmSMMUFirmwareHeldIds_t HeldIds[ARM_SMMU_MAX_HELD_IDS];
};

#endif //!__ARM_SMMU_DISCOVERY_PRIVATE_H__
