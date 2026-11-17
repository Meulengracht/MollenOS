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

#ifndef __ARM64_ACPI_IORT_H__
#define __ARM64_ACPI_IORT_H__

#include <os/osdefs.h>
#include <os/types/dma.h>

#include "../smmu/description.h"
#include "../common/its.h"

/**
 * @brief Checked copy of the firmware I/O Remapping Table (IORT).
 *
 * Firmware writes the IORT to show how devices connect to memory and interrupt
 * hardware. Devices can read and write RAM on their own using DMA (direct
 * memory access), and an SMMU (System Memory Management Unit) sits on that
 * path to translate device addresses into real physical addresses. Devices
 * raise interrupts by writing a small message to a special memory address,
 * known as an MSI (message-signaled interrupt). An ITS (Interrupt Translation
 * Service) block receives those messages and delivers interrupts to CPUs.
 *
 * Each node in this catalogue stands for one hardware block, and its mappings
 * show how a device's numeric ID changes as requests pass from one block to
 * the next. Building the catalogue only reads and checks the table. No hardware
 * is touched or turned on.
 */
DECL_STRUCT(ArmIortCatalogue);

// Hardware block kinds defined by the IORT table.
enum ArmIortNodeType {
    ArmIortItsGroup,       // Group of ITS interrupt controllers
    ArmIortNamedComponent, // Fixed non-PCI device named by an ACPI path
    ArmIortPciRoot,        // PCI host bridge for one PCI segment
    ArmIortSmmu,           // Older SMMUv1 or SMMUv2 unit (such as MMU-500)
    ArmIortSmmuV3,         // SMMUv3 address translation unit
    ArmIortPmcg,           // Performance counter unit on an SMMU or root
    ArmIortRmr             // Memory range reserved by firmware across boot
};

/**
 * @brief One ID mapping from a hardware node to a downstream node.
 *
 * InputFirst and InputLast mark the inclusive range of incoming device IDs. A
 * matching ID becomes OutputFirst + (input - InputFirst) on the target node at
 * TargetIndex in the catalogue. When MapsAllInputsToOne is set, the input range
 * is ignored and every ID maps to OutputFirst.
 *
 * IsOwnInterruptMapping marks mappings used for the internal interrupts of an
 * SMMU or a performance counter unit (PMCG). Skip these entries when tracing a
 * downstream device, since they carry the controller's own interrupts.
 *
 * Reserved Memory Range (RMR) mappings point to devices that must keep using
 * memory firmware set aside before boot (such as a bootloader framebuffer).
 * RMR entries use only their output range, starting at OutputFirst and ending
 * at OutputFirst + (InputLast - InputFirst).
 */
typedef struct ArmIortMapping {
    uint32_t InputFirst;
    uint32_t InputLast;
    uint32_t OutputFirst;
    uint32_t TargetIndex;
    bool     MapsAllInputsToOne;
    bool     IsOwnInterruptMapping;
} ArmIortMapping_t;

/**
 * @brief Parsed IORT description of a single hardware block.
 *
 * Offset gives the node's byte position in the original ACPI table, which older
 * tables also use instead of Identifier to tell nodes apart. All pointer fields
 * belong to the catalogue and stay valid until ArmIortDestroy frees it.
 *
 * Segment is the PCI segment group number for a PCI root. AcpiPath holds the
 * ACPI device path (such as "\_SB.ETH0") for a named non-PCI device.
 *
 * AddressLimit is the highest physical memory address devices under this node
 * can reach. CachePolicy, HardwareCoherent, and UsesDeviceMemoryAttributes
 * record whether device DMA stays in sync with CPU caches automatically or
 * needs software cache flushes. HardwareCoherent and UsesDeviceMemoryAttributes
 * are the two memory flag bits from the IORT node (CPM and DACS). DmaStatus
 * reports whether our kernel supports DMA through this node. Interrupt lookups
 * still work even if DmaStatus is an error.
 *
 * Smmu holds the parsed register and interrupt layout when this node is a
 * supported SMMU. For an ITS group, ItsIdentifiers lists its controller IDs
 * in firmware order, matching the IDs copied from the MADT (the ACPI table
 * that lists CPUs and interrupt controllers).
 */
typedef struct ArmIortNode {
    enum ArmIortNodeType        Type;
    uint32_t                    Offset;
    uint32_t                    Identifier;
    uint32_t                    MappingCount;
    const ArmIortMapping_t*     Mappings;
    const char*                 AcpiPath;
    uint32_t                    Segment;
    enum SHMDeviceCachePolicy   CachePolicy;
    uint64_t                    AddressLimit;
    bool                        HardwareCoherent;
    bool                        UsesDeviceMemoryAttributes;
    oserr_t                     DmaStatus;
    const ArmSMMUDescription_t* Smmu;
    uint32_t                    ItsCount;
    const uint32_t*             ItsIdentifiers;
} ArmIortNode_t;

/**
 * @brief Where a PCI device sends its message-signaled interrupts (MSIs).
 *
 * ItsGroup points to the catalogue node listing the ITS controllers that can
 * handle the device. DeviceId is the ID the device uses when talking to those
 * controllers, after any bridge or SMMU translations in the IORT have been
 * applied. Keep the catalogue alive while reading ItsGroup.
 *
 * Finding a route does not check whether any ITS controller in the group is
 * initialized, or whether the device can perform DMA. The caller must still
 * pick a working controller from ItsGroup before assigning interrupts.
 */
typedef struct ArmIortMsiRoute {
    const ArmIortNode_t* ItsGroup;
    uint32_t             DeviceId;
} ArmIortMsiRoute_t;

/**
 * @brief Reads the IORT once at boot and caches the shared catalogue.
 *
 * Run this on the boot CPU after ACPI tables and kernel memory allocation are
 * available, before starting secondary CPUs or scanning PCI buses. The result
 * stays fixed until shutdown, even if discovery fails. Later calls return that
 * saved status without parsing the table again, and no hardware is configured.
 *
 * @return Status from ArmIortCreate.
 */
KERNELABI oserr_t KERNELAPI
ArmIortInitialize(void);

/**
 * @brief Returns the shared boot IORT catalogue.
 *
 * ArmIortInitialize must run before calling this. The returned catalogue is
 * read-only and remains valid for the rest of the boot.
 *
 * @param catalogueOut Receives the catalogue pointer on success.
 * @return OS_EOK on success, OS_EINVALPARAMS if catalogueOut is NULL, or the
 *         error saved during initialization.
 */
KERNELABI oserr_t KERNELAPI
ArmIortGetCatalogue(
    _Out_ const ArmIortCatalogue_t** catalogueOut);

/**
 * @brief Parses and validates the firmware IORT into a new catalogue.
 *
 * Used during boot and by host firmware tests. Before returning, this routine
 * releases the ACPI table back to ACPICA because everything needed has been
 * copied out.
 *
 * @param catalogueOut Must point to NULL. Receives the new catalogue on success.
 * @return OS_EOK on success, OS_ENOENT if the system has no IORT, OS_EBUSY if
 *         *catalogueOut is already set, OS_ENOTSUPPORTED for an unknown table
 *         layout or a table exceeding our static node limits, or OS_EINVALPARAMS
 *         for bad arguments or malformed table data. Memory allocation and ACPI
 *         lookup errors are returned directly. On failure, *catalogueOut stays
 *         untouched and all partial allocations are freed.
 */
KERNELABI oserr_t KERNELAPI
ArmIortCreate(
    _InOut_ ArmIortCatalogue_t** catalogueOut);

/**
 * @brief Frees a catalogue created by ArmIortCreate and sets *catalogue to NULL.
 *
 * Call this only after all readers are done with the catalogue and its nodes.
 * Do not pass the shared boot catalogue here, since callers expect it to live
 * until shutdown.
 *
 * @param catalogue Pointer to the catalogue to free. NULL or a pointer to NULL
 *                  is ignored.
 */
KERNELABI void KERNELAPI
ArmIortDestroy(
    _InOut_ ArmIortCatalogue_t** catalogue);

/**
 * @brief Returns the number of nodes in the catalogue, or 0 if catalogue is NULL.
 */
KERNELABI uint32_t KERNELAPI
ArmIortGetNodeCount(
    _In_ const ArmIortCatalogue_t* catalogue);

/**
 * @brief Returns a read-only node by zero-based index in the catalogue.
 *
 * The returned pointer stays valid until the catalogue is destroyed.
 *
 * @return The node pointer, or NULL if catalogue is NULL or index is out of range.
 */
KERNELABI KERNELAPI const ArmIortNode_t*
ArmIortGetNode(
    _In_ const ArmIortCatalogue_t* catalogue,
    _In_ uint32_t                  index);

/**
 * @brief Finds the highest physical address devices on a PCI segment can reach
 *        with direct DMA.
 *
 * Direct DMA means devices read and write real physical memory addresses with
 * no SMMU translating them. This requires exactly one PCI root node for the
 * segment, with hardware settings that keep device DMA in sync with the CPU
 * caches. Any ID mappings on that root must go straight to ITS groups and cover
 * all 65,536 16-bit PCI requester IDs with no gaps.
 *
 * This checks only the IORT. Before enabling DMA, the caller must also check
 * the ACPI _CCA cache setting, verify no _DMA address windows restrict the
 * bus, and make sure no VIOT (Virtual I/O Translation Table) is present.
 *
 * @param catalogue       Checked IORT catalogue.
 * @param segment         PCI segment group number.
 * @param addressLimitOut Receives the highest usable physical address on success.
 * @return OS_EOK on success, OS_EINVALPARAMS if an argument is NULL, or
 *         OS_ENOTSUPPORTED if the segment is missing or fails these checks.
 */
KERNELABI oserr_t KERNELAPI
ArmIortGetPciDirectLimit(
    _In_  const ArmIortCatalogue_t* catalogue,
    _In_  uint32_t                  segment,
    _Out_ uint64_t*                 addressLimitOut);

/**
 * @brief Resolves which ITS group and device ID handle interrupts for a PCI device.
 *
 * A PCI requester ID packs a device's bus, device, and function numbers into a
 * 16-bit value. If an upstream bridge rewrites traffic under its own ID, pass
 * the requester ID that arrives at the PCI root.
 *
 * Starting from the matching PCI root, this traces mappings through at most one
 * SMMU to an ITS group. DMA settings are ignored during this lookup. Keep the
 * catalogue alive while reading the returned ItsGroup, and pick and initialize
 * a controller from it separately.
 *
 * @param catalogue   Checked IORT catalogue.
 * @param segment     PCI segment group number.
 * @param requesterId 16-bit PCI requester ID seen at the root.
 * @param routeOut    Receives the target ITS group and translated device ID.
 * @return OS_EOK on success, OS_ENOENT if no mapping covers requesterId,
 *         OS_ENOTSUPPORTED if the path is unsupported or the group is empty, or
 *         OS_EINVALPARAMS if an argument is NULL or multiple mappings match.
 */
KERNELABI oserr_t KERNELAPI
ArmIortResolvePciMsi(
    _In_  const ArmIortCatalogue_t* catalogue,
    _In_  uint32_t                  segment,
    _In_  uint16_t                  requesterId,
    _Out_ ArmIortMsiRoute_t*         routeOut);

/**
 * @brief Looks up the MADT register range for one ITS controller in a group.
 *
 * Pass an ItsGroup from ArmIortResolvePciMsi and one controller ID from its
 * ItsIdentifiers array. ACPI platform discovery must already have finished.
 *
 * This reads only the table entries copied at boot. The caller still needs to
 * initialize the controller hardware before handing out interrupts.
 *
 * @param group         ITS group node from the catalogue.
 * @param identifier    Controller ID taken from group->ItsIdentifiers.
 * @param controllerOut Receives a copy of the controller ID and register range.
 * @return OS_EOK on success, OS_EINVALPARAMS if an argument is NULL or group is
 *         not an ITS group, or OS_ENOENT if the ID is not in the group or MADT.
 */
KERNELABI oserr_t KERNELAPI
ArmIortGetItsController(
    _In_  const ArmIortNode_t*   group,
    _In_  uint32_t               identifier,
    _Out_ Arm64ItsDescription_t* controllerOut);

#endif //!__ARM64_ACPI_IORT_H__
