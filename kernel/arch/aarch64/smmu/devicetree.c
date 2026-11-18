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
 * ARM SMMU Device Tree Discovery
 * Reads SMMU nodes, interrupt lines, DMA bus windows, and device ID mappings
 * from a parsed Device Tree into ArmSMMUDiscovery_t without touching hardware.
 */

#include "../smmu/discovery-private.h"
#include <ddk/types/interrupt.h>
#include <debug.h>
#include <fdt/dma.h>
#include <fdt/reader.h>
#include <heap.h>
#include <log.h>
#include <string.h>

// Device Tree GIC bindings number Shared Peripheral Interrupts (SPIs) starting
// from 0, while the GIC hardware maps SPIs to interrupt IDs 32 through 1019
// (988 lines). IDs 1020..1023 are special GIC status values, never real wires.
#define SMMU_GIC_SPI_BASE       32U
#define SMMU_GIC_SPI_COUNT      988U

// SMMUv3 hardware exposes two 64 KiB control pages (Page 0 and Page 1).
#define SMMU_V3_REGISTER_LENGTH 0x20000U

// SMMUv3 nodes list up to four interrupts named in interrupt-names. Bit flags
// let us catch duplicate names and prevent mixing "combined" with other lines.
#define SMMU_V3_MAX_INTERRUPTS  4U
#define SMMU_V3_NAME_EVENTQ     0x01U
#define SMMU_V3_NAME_GERROR     0x02U
#define SMMU_V3_NAME_PRIQ       0x04U
#define SMMU_V3_NAME_CMDQ_SYNC  0x08U
#define SMMU_V3_NAME_COMBINED   0x10U

// Third cell in a Device Tree GIC interrupt specifier selects edge vs level.
#define SMMU_GIC_EDGE_RISING    1U
#define SMMU_GIC_LEVEL_HIGH     4U

// An MMU-500 provides up to 128 hardware translation slots (context banks),
// each able to carry its own fault interrupt line.
#define SMMU_500_MAX_CONTEXTS   128U

/**
 * @brief Scratch record for one enabled SMMU node found in the Device Tree.
 *
 * Units that use features our kernel cannot drive stay in this table with a
 * non-OK Status so devices pointing at them fail cleanly as unsupported.
 * Index holds the unit's position in ArmSMMUDiscovery.Units, or UINT32_MAX
 * when unsupported. Cells is #iommu-cells, and Mask holds stream-match-mask
 * for an MMU-500 that takes one cell per device entry.
 */
struct __SmmuTreeUnit {
    const DeviceTreeNode_t* Node;
    uint32_t                Index;
    uint32_t                Cells;
    uint32_t                Mask;
    uint64_t                RegisterBase;
    uint64_t                RegisterLength;
    oserr_t                 Status;
};

// Scratch state shared while walking the Device Tree.
struct __SmmuTreeParser {
    const DeviceTree_t*     Tree;
    ArmSMMUDiscovery_t*     Discovery;
    uint32_t                UnitCount;
    struct __SmmuTreeUnit   Units[ARM_SMMU_MAX_UNITS];
};

// Reads a single big-endian 32-bit integer property ("cell") from node.
// Returns OS_ENOENT if missing, or OS_EINVALPARAMS if its size is not 4 bytes.
static oserr_t
__ReadCell(
    _In_  const DeviceTreeNode_t* node,
    _In_  const char*             name,
    _Out_ uint32_t*               value)
{
    const DeviceTreeProperty_t* property = DeviceTreeGetProperty(node, name);

    if (property == NULL) {
        return OS_ENOENT;
    }

    if (property->Length != 4) {
        return OS_EINVALPARAMS;
    }

    *value = FdtReadBe32(property->Value);
    return OS_EOK;
}

// Reads a Device Tree boolean property. In FDT, a boolean is an empty
// property whose presence means true and absence means false. Any non-zero
// payload length is malformed.
static oserr_t
__ReadBoolean(
    _In_  const DeviceTreeNode_t* node,
    _In_  const char*             name,
    _Out_ bool*                  value)
{
    const DeviceTreeProperty_t* property = DeviceTreeGetProperty(node, name);

    if (property != NULL && property->Length != 0) {
        return OS_EINVALPARAMS;
    }
    *value = property != NULL;
    return OS_EOK;
}

// Validates that a Device Tree string (or list of NUL-terminated strings when
// list is true) is non-empty, NUL-terminated, and free of empty entries.
static oserr_t
__CheckStrings(
    _In_ const DeviceTreeNode_t* node,
    _In_ const char*             name,
    _In_ bool                    list)
{
    const DeviceTreeProperty_t* property = DeviceTreeGetProperty(node, name);
    const char*                bytes;

    if (property == NULL) {
        return OS_EOK;
    }

    bytes = property->Value;
    if (property->Length == 0 || bytes[property->Length - 1] != 0) {
        return OS_EINVALPARAMS;
    }

    for (uint32_t offset = 0; offset < property->Length;) {
        uint32_t length = (uint32_t)strlen(bytes + offset) + 1;

        if (length == 1 || (!list && length != property->Length)) {
            return OS_EINVALPARAMS;
        }
        offset += length;
    }
    return OS_EOK;
}

/**
 * Verifies that an interrupt parent is an enabled GIC supported by the kernel.
 * Interrupt maps and non-GIC parents are rejected because their interrupt
 * numbers cannot be passed straight to our GIC driver.
 */
static oserr_t
__CheckGicController(
    _In_ const DeviceTreeReference_t* reference)
{
    oserr_t status;
    bool    controller;

    if (!DeviceTreeIsEnabled(reference->Node)) {
        return OS_EINVALPARAMS;
    }

    status = __ReadBoolean(reference->Node, "interrupt-controller", &controller);
    if (status != OS_EOK) {
        return status;
    }

    if (!controller) {
        return OS_EINVALPARAMS;
    }

    // Require a 3-cell SPI specifier <type number flags>. Cell 0 must be 0
    // for a Shared Peripheral Interrupt (1 would be a per-CPU PPI).
    if ((!DeviceTreeIsCompatible(reference->Node, "arm,gic-v3") &&
         !DeviceTreeIsCompatible(reference->Node, "arm,gic-400") &&
         !DeviceTreeIsCompatible(reference->Node, "arm,cortex-a15-gic")) ||
        DeviceTreeGetProperty(reference->Node, "interrupt-map") != NULL ||
        reference->CellCount != 3 || reference->Cells[0] != 0) {
        return OS_ENOTSUPPORTED;
    }
    return OS_EOK;
}

/**
 * Reads the interrupt entry at index from node and converts its 3-cell GIC
 * specifier into an ArmSMMUInterrupt_t. Returns OS_ENOENT when no entry exists
 * at that index.
 */
static oserr_t
__ReadInterrupt(
    _In_  const struct __SmmuTreeParser* parser,
    _In_  const DeviceTreeNode_t*        node,
    _In_  uint32_t                       index,
    _Out_ ArmSMMUInterrupt_t*            interrupt)
{
    DeviceTreeReference_t reference;
    oserr_t               status;

    status = DeviceTreeReadInterrupt(parser->Tree, node, index, &reference);
    if (status != OS_EOK) {
        return status;
    }

    status = __CheckGicController(&reference);
    if (status != OS_EOK) {
        return status;
    }

    if (reference.Cells[1] >= SMMU_GIC_SPI_COUNT) {
        return OS_EINVALPARAMS;
    }

    // Accept rising-edge and active-high level SPIs, stored using the kernel's
    // ACPI-style trigger flags.
    if (reference.Cells[2] != SMMU_GIC_EDGE_RISING && reference.Cells[2] != SMMU_GIC_LEVEL_HIGH) {
        return OS_ENOTSUPPORTED;
    }

    interrupt->Line = reference.Cells[1] + SMMU_GIC_SPI_BASE;
    interrupt->Flags = INTERRUPT_ACPICONFORM_PRESENT;
    if (reference.Cells[2] == SMMU_GIC_LEVEL_HIGH) {
        interrupt->Flags |= INTERRUPT_ACPICONFORM_TRIGGERMODE;
    }
    return OS_EOK;
}

/**
 * Maps one SMMUv3 interrupt-names entry onto its field in description.
 * Queue lines our driver does not use yet ("priq" and "cmdq-sync") are
 * validated and dropped. A "combined" wire drives both EventQueue and
 * GlobalError. bitOut receives the SMMU_V3_NAME_* flag for duplicate checks.
 */
static oserr_t
__StoreV3Interrupt(
    _In_  const char*               name,
    _In_  const ArmSMMUInterrupt_t* interrupt,
    _In_  ArmSMMUDescription_t*     description,
    _Out_ uint32_t*                 bitOut)
{
    if (!strcmp(name, "eventq")) {
        *bitOut = SMMU_V3_NAME_EVENTQ;
        description->V3.EventQueue = *interrupt;
    } else if (!strcmp(name, "gerror")) {
        *bitOut = SMMU_V3_NAME_GERROR;
        description->V3.GlobalError = *interrupt;
    } else if (!strcmp(name, "priq")) {
        *bitOut = SMMU_V3_NAME_PRIQ;
    } else if (!strcmp(name, "cmdq-sync")) {
        *bitOut = SMMU_V3_NAME_CMDQ_SYNC;
    } else if (!strcmp(name, "combined")) {
        *bitOut = SMMU_V3_NAME_COMBINED;
        description->V3.EventQueue = *interrupt;
        description->V3.GlobalError = *interrupt;
    } else {
        return OS_ENOTSUPPORTED;
    }
    return OS_EOK;
}

/**
 * Reads the interrupts of an SMMUv3 node using interrupt-names, since Device
 * Tree allows firmware to list SMMUv3 lines in any order. Each name may appear
 * once, a "combined" line must stand alone, and unnamed trailing interrupts are
 * rejected.
 */
static oserr_t
__ReadV3Interrupts(
    _In_  const struct __SmmuTreeParser* parser,
    _In_  const DeviceTreeNode_t*        node,
    _Out_ ArmSMMUDescription_t*          description)
{
    const DeviceTreeProperty_t* names = DeviceTreeGetProperty(node, "interrupt-names");
    ArmSMMUInterrupt_t          interrupt;
    uint32_t                    offset = 0;
    uint32_t                    count = 0;
    uint32_t                    seen = 0;
    oserr_t                     status;

    // Default missing wires to UINT32_MAX so callers can tell which lines exist.
    description->V3.EventQueue.Line = UINT32_MAX;
    description->V3.GlobalError.Line = UINT32_MAX;

    status = __CheckStrings(node, "interrupt-names", true);
    if (status != OS_EOK) {
        return status;
    }

    while (names != NULL && offset < names->Length) {
        const char* name = (const char*)names->Value + offset;
        uint32_t    bit;

        if (++count > SMMU_V3_MAX_INTERRUPTS) {
            return OS_ENOTSUPPORTED;
        }

        status = __ReadInterrupt(parser, node, count - 1, &interrupt);
        if (status != OS_EOK) {
            return status == OS_ENOENT ? OS_EINVALPARAMS : status;
        }

        status = __StoreV3Interrupt(name, &interrupt, description, &bit);
        if (status != OS_EOK) {
            return status;
        }

        // Reject repeated names and any mix of "combined" with separate lines.
        if ((seen & bit) != 0 || (((seen | bit) & SMMU_V3_NAME_COMBINED) != 0 && count != 1)) {
            return OS_EINVALPARAMS;
        }

        seen |= bit;
        offset += (uint32_t)strlen(name) + 1;
    }

    // Make sure no extra interrupts sit past the end of interrupt-names.
    status = __ReadInterrupt(parser, node, count, &interrupt);
    if (status != OS_ENOENT) {
        return status == OS_EOK ? OS_EINVALPARAMS : status;
    }
    return OS_EOK;
}

/**
 * Reads the interrupts of an MMU-500 node. #global-interrupts tells how many
 * entries at the front of interrupts signal controller-wide faults. Every
 * remaining entry belongs to one hardware translation slot (context bank) in
 * order, and at least one context bank interrupt is required.
 */
static oserr_t
__Read500Interrupts(
    _In_  const struct __SmmuTreeParser* parser,
    _In_  const DeviceTreeNode_t*        node,
    _Out_ ArmSMMUDescription_t*          description)
{
    ArmSMMUInterrupt_t interrupt;
    uint32_t           globals;
    uint32_t           count = 0;
    oserr_t            status;

    description->Mmu500.GlobalFault.Line = UINT32_MAX;
    description->Mmu500.ConfigurationFault.Line = UINT32_MAX;
    
    status = __ReadCell(node, "#global-interrupts", &globals);
    if (status != OS_EOK) {
        return status == OS_ENOENT ? OS_EINVALPARAMS : status;
    }

    // The generic DT binding does not name a second global interrupt's role.
    // Keep ConfigurationFault absent until a platform binding identifies it.
    if (globals > 1) {
        return OS_ENOTSUPPORTED;
    }

    for (;;) {
        status = __ReadInterrupt(parser, node, count, &interrupt);
        if (status == OS_ENOENT) {
            break;
        } else if (status != OS_EOK) {
            return status;
        }

        if (count < globals) {
            description->Mmu500.GlobalFault = interrupt;
        } else {
            if (count - globals == SMMU_500_MAX_CONTEXTS) {
                return OS_ENOTSUPPORTED;
            }
            description->Mmu500.ContextInterrupts[count - globals] = interrupt;
        }
        count++;
    }

    if (count <= globals) {
        return OS_EINVALPARAMS;
    }

    description->Mmu500.ContextInterruptCount = count - globals;
    return OS_EOK;
}

/**
 * Reads #iommu-cells (how many 32-bit numbers follow a reference to this SMMU
 * in a device's iommus or iommu-map property). SMMUv3 uses one cell for the
 * device stream ID. MMU-500 uses either one cell paired with the controller's
 * stream-match-mask, or two cells providing <stream-id mask> per reference.
 */
static oserr_t
__ReadUnitIdFormat(
    _In_    const DeviceTreeNode_t* node,
    _In_    enum ArmSMMUKind        kind,
    _InOut_ struct __SmmuTreeUnit*  unit)
{
    oserr_t status;

    status = __ReadCell(node, "#iommu-cells", &unit->Cells);
    if (status != OS_EOK) {
        return status == OS_ENOENT ? OS_EINVALPARAMS : status;
    }
    
    if ((kind == ArmSMMUv3 && unit->Cells != 1) ||
        (kind == ArmSMMU500 && unit->Cells != 1 && unit->Cells != 2)) {
        return OS_ENOTSUPPORTED;
    }

    if (kind == ArmSMMU500 && unit->Cells == 1) {
        status = __ReadCell(node, "stream-match-mask", &unit->Mask);
        if (status != OS_EOK && status != OS_ENOENT) {
            return status;
        }
    }
    return OS_EOK;
}

/**
 * Reads the full hardware description of one SMMU node. Returns OS_ENOTSUPPORTED
 * for features or vendor register quirks the kernel cannot drive, and
 * OS_EINVALPARAMS for malformed Device Tree properties.
 */
static oserr_t
__ReadUnit(
    _In_    const struct __SmmuTreeParser* parser,
    _InOut_ struct __SmmuTreeUnit*         unit,
    _Out_   ArmSMMUDescription_t*          description)
{
    const DeviceTreeNode_t* node = unit->Node;
    oserr_t                 status;

    description->Kind = DeviceTreeIsCompatible(node, "arm,smmu-v3") ? ArmSMMUv3 : ArmSMMU500;
    status = __ReadUnitIdFormat(node, description->Kind, unit);
    if (status != OS_EOK) {
        return status;
    }

    description->RegisterBase = unit->RegisterBase;
    description->RegisterLength = unit->RegisterLength;
    // Standard SMMUv3 uses two 64 KiB register pages (128 KiB total).
    if (description->Kind == ArmSMMUv3 && description->RegisterLength != SMMU_V3_REGISTER_LENGTH) {
        return OS_ENOTSUPPORTED;
    }

    // dma-coherent on the SMMU node marks its own page-table walks coherent
    // with CPU caches.
    status = __ReadBoolean(node, "dma-coherent", &description->TableCoherent);
    if (status != OS_EOK) {
        return status;
    }

    // These vendor quirk properties alter how SMMU registers are laid out or
    // accessed. Reject them here so a generic driver never touches unsupported hardware.
    if (DeviceTreeGetProperty(node, "cavium,cn9900-broken-page1-regspace") != NULL ||
        DeviceTreeGetProperty(node, "calxeda,smmu-secure-config-access") != NULL) {
        return OS_ENOTSUPPORTED;
    }

    if (description->Kind == ArmSMMUv3) {
        return __ReadV3Interrupts(parser, node, description);
    }
    return __Read500Interrupts(parser, node, description);
}

// Validates that compatible, status, and device_type strings on node are
// properly NUL-terminated before any helper searches them.
static oserr_t
__CheckNodeStrings(
    _In_ const DeviceTreeNode_t* node)
{
    oserr_t status;

    status = __CheckStrings(node, "compatible", true);
    if (status != OS_EOK) {
        return status;
    }

    status = __CheckStrings(node, "status", false);
    if (status != OS_EOK) {
        return status;
    }
    return __CheckStrings(node, "device_type", false);
}

/**
 * Reads the single MMIO register range (reg property) of an SMMU node.
 * Rejects nodes with a second register range or a zero-length range because
 * extra regions indicate a controller layout our driver does not support.
 */
static oserr_t
__ReadUnitRegisters(
    _InOut_ struct __SmmuTreeUnit* unit)
{
    uint64_t unusedBase;
    uint64_t unusedLength;
    oserr_t  status;

    // Make sure no second reg entry (index 1) is present.
    status = DeviceTreeReadRegisterRaw(unit->Node, 1, &unusedBase, &unusedLength);
    if (status != OS_ENOENT) {
        return status == OS_EOK ? OS_ENOTSUPPORTED : status;
    }

    status = DeviceTreeReadRegister(unit->Node, 0, &unit->RegisterBase, &unit->RegisterLength);
    if (status != OS_EOK) {
        return status == OS_ENOENT ? OS_ENOTSUPPORTED : status;
    }

    if (unit->RegisterLength == 0) {
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

/**
 * Checks that the newest SMMU's register range does not overlap any unit
 * collected earlier. unit already sits at the end of parser->Units, so the
 * loop stops at UnitCount - 1.
 */
static oserr_t
__CheckUnitOverlap(
    _In_ const struct __SmmuTreeParser* parser,
    _In_ const struct __SmmuTreeUnit*   unit)
{
    for (uint32_t i = 0; i + 1 < parser->UnitCount; i++) {
        const struct __SmmuTreeUnit* other = &parser->Units[i];

        if (unit->RegisterBase < other->RegisterBase + other->RegisterLength &&
            other->RegisterBase < unit->RegisterBase + unit->RegisterLength) {
            return OS_EINVALPARAMS;
        }
    }
    return OS_EOK;
}

/**
 * Scans the Device Tree for enabled SMMU nodes and records each one.
 * Supported units go into discovery->Units. Units needing unsupported features
 * stay in parser->Units with an error Status so device routes targeting them
 * fail cleanly. Also validates string properties across every node and caps
 * total node count to bound work on malformed trees.
 */
static oserr_t
__CollectUnits(
    _InOut_ struct __SmmuTreeParser* parser)
{
    ArmSMMUDiscovery_t*     discovery = parser->Discovery;
    const DeviceTreeNode_t* node;
    uint32_t                count = 0;

    for (node = parser->Tree->Nodes; node != NULL; node = node->NextNode) {
        ArmSMMUDescription_t   description = { 0 };
        struct __SmmuTreeUnit* unit;
        oserr_t                status;

        if (++count > ARM_SMMU_MAX_NODES) {
            return OS_ENOTSUPPORTED;
        }

        status = __CheckNodeStrings(node);
        if (status != OS_EOK) {
            return status;
        }

        if (!DeviceTreeIsEnabled(node) ||
            (!DeviceTreeIsCompatible(node, "arm,smmu-v3") && !DeviceTreeIsCompatible(node, "arm,mmu-500"))) {
            continue;
        }

        if (parser->UnitCount == ARM_SMMU_MAX_UNITS) {
            return OS_ENOTSUPPORTED;
        }

        unit = &parser->Units[parser->UnitCount++];
        unit->Node = node;
        unit->Index = UINT32_MAX;

        // Read and overlap-check MMIO registers even for unsupported units so
        // a broken node cannot silently cover the same registers as a good one.
        status = __ReadUnitRegisters(unit);
        if (status != OS_EOK) {
            return status;
        }

        status = __CheckUnitOverlap(parser, unit);
        if (status != OS_EOK) {
            return status;
        }

        status = __ReadUnit(parser, unit, &description);
        if (status != OS_EOK && status != OS_ENOTSUPPORTED) {
            return status;
        }

        unit->Status = status;
        if (status != OS_EOK) {
            WARNING("SMMU DT: unit %s requires unsupported features", node->Name);
            continue;
        }

        unit->Index = discovery->UnitCount;
        discovery->Units[discovery->UnitCount++] = description;
    }
    return OS_EOK;
}

// Reads a cell-count property such as #address-cells, falling back to
// defaultValue if the property is absent on node.
static oserr_t
__ReadCellsOrDefault(
    _In_  const DeviceTreeNode_t* node,
    _In_  const char*             name,
    _In_  uint32_t                defaultValue,
    _Out_ uint32_t*               cells)
{
    oserr_t status = __ReadCell(node, name, cells);

    if (status == OS_ENOENT) {
        *cells = defaultValue;
        return OS_EOK;
    }
    return status;
}

/**
 * Fills property with the dma-ranges bytes and address layout of bus. Each
 * entry encodes <child-address parent-address length>. Child addresses use
 * #address-cells on bus (a PCI address for PCI buses), parent addresses use
 * #address-cells on bus->Parent, and length uses #size-cells on bus. The
 * decoder validates the cell counts.
 */
static oserr_t
__ReadDmaProperty(
    _In_  const DeviceTreeNode_t* bus,
    _In_  bool                    pci,
    _Out_ FdtDmaProperty_t*       property)
{
    const DeviceTreeProperty_t* ranges = DeviceTreeGetProperty(bus, "dma-ranges");
    oserr_t                     status;

    *property = (FdtDmaProperty_t) {
        .ChildFormat = pci ? FdtDmaAddressPci : FdtDmaAddressSimple,
        .ParentFormat = FdtDmaAddressSimple
    };

    if (ranges != NULL) {
        property->Bytes = ranges->Value;
        property->Length = ranges->Length;
    }

    status = __ReadCellsOrDefault(bus, "#address-cells", 2, &property->ChildCells);
    if (status != OS_EOK) {
        return status;
    }

    status = __ReadCellsOrDefault(bus->Parent, "#address-cells", 2, &property->ParentCells);
    if (status != OS_EOK) {
        return status;
    }
    return __ReadCellsOrDefault(bus, "#size-cells", 1, &property->SizeCells);
}

/**
 * Lowers limit using the dma-ranges property on bus, which describes which DMA
 * addresses can travel upstream toward RAM. Missing dma-ranges returns
 * OS_ENOTSUPPORTED because DMA reachability cannot be assumed. An empty
 * property means addresses pass 1:1, bounded only by child and parent cell
 * widths. Listed windows are supported only as one window that maps address 0
 * to address 0, since ArmSMMUDeviceRoute carries a single 0-based limit. Set
 * pci to true for PCI host bridges using 3-cell child addresses.
 */
static oserr_t
__ReadDmaWindow(
    _In_    const DeviceTreeNode_t* bus,
    _In_    bool                    pci,
    _InOut_ uint64_t*               limit)
{
    FdtDmaProperty_t   property;
    FdtDmaBusWindows_t windows;
    oserr_t            status;

    status = __ReadDmaProperty(bus, pci, &property);
    if (status != OS_EOK) {
        return status;
    }

    status = FdtDmaDecode(&property, &windows);
    if (status != OS_EOK) {
        return status;
    }

    switch (windows.State) {
        case FdtDmaPropertyIdentity:
            if (windows.ChildLimit < *limit) {
                *limit = windows.ChildLimit;
            }
            return OS_EOK;
        case FdtDmaPropertyWindows:
            if (windows.Count != 1 || windows.Windows[0].ChildBase != 0 ||
                windows.Windows[0].ParentBase != 0) {
                return OS_ENOTSUPPORTED;
            }

            if (windows.Windows[0].Length - 1 < *limit) {
                *limit = windows.Windows[0].Length - 1;
            }
            return OS_EOK;
        default:
            // Missing windows prove nothing about what the bus can reach.
            return OS_ENOTSUPPORTED;
    }
}

/**
 * Checks dma-coherent and dma-noncoherent on node. The closest node to the
 * device that specifies either flag sets source->CachePolicy. Having both flags
 * on the same node is invalid.
 */
static oserr_t
__ReadCacheFlags(
    _In_    const DeviceTreeNode_t* node,
    _InOut_ ArmSMMUSource_t* source,
    _InOut_ bool*                   cacheKnown)
{
    bool    coherent;
    bool    noncoherent;
    oserr_t status;

    status = __ReadBoolean(node, "dma-coherent", &coherent);
    if (status != OS_EOK) {
        return status;
    }

    status = __ReadBoolean(node, "dma-noncoherent", &noncoherent);
    if (status != OS_EOK) {
        return status;
    }

    if (coherent && noncoherent) {
        return OS_EINVALPARAMS;
    }

    if (!*cacheKnown && (coherent || noncoherent)) {
        source->CachePolicy = coherent ? SHMDeviceCacheCoherent : SHMDeviceCacheNonCoherent;
        *cacheKnown = true;
    }
    return OS_EOK;
}

// Returns true if node has device_type "pci". A NULL node is not PCI.
static bool
__IsPciNode(
    _In_ const DeviceTreeNode_t* node)
{
    const char* type = node != NULL ? DeviceTreeReadString(node, "device_type") : NULL;

    return type != NULL && strcmp(type, "pci") == 0;
}

/**
 * Checks whether node redirects DMA outside the simple bus path our discovery
 * models, using the shared FdtDmaNodeIsDirect rules. The device node itself
 * uses iommus or iommu-map for its own route, so those are ignored for it,
 * while any parent bus with those properties is unsupported. A node with
 * memory-region is unsupported too.
 */
static bool
__HasUnsupportedDmaRedirect(
    _In_ const DeviceTreeNode_t* device,
    _In_ const DeviceTreeNode_t* node)
{
    const DeviceTreeProperty_t* compatible = DeviceTreeGetProperty(node, "compatible");
    FdtDmaNode_t                description = {
        .Compatible = compatible != NULL ? compatible->Value : NULL,
        .CompatibleLength = compatible != NULL ? compatible->Length : 0,
        .IsRoot = node->Parent == NULL,
        .IsPci = __IsPciNode(node),
        .ParentIsPci = __IsPciNode(node->Parent),
        .Disabled = !DeviceTreeIsEnabled(node),
        .HasIommus = node != device && DeviceTreeGetProperty(node, "iommus") != NULL,
        .HasIommuMap = node != device && DeviceTreeGetProperty(node, "iommu-map") != NULL,
        .HasDmaParent = DeviceTreeGetProperty(node, "dma-parent") != NULL,
        .HasDmaRanges = DeviceTreeGetProperty(node, "dma-ranges") != NULL
    };

    return DeviceTreeGetProperty(node, "memory-region") != NULL ||
           !FdtDmaNodeIsDirect(&description, NULL);
}

// Applies the root node's #address-cells width to source->AddressLimit so a
// 32-bit root bus clamps direct children that had no intermediate dma-ranges.
static oserr_t
__ReadRootAddressWidth(
    _In_    const DeviceTreeNode_t* root,
    _InOut_ ArmSMMUSource_t* source)
{
    uint32_t cells;
    oserr_t  status;

    status = __ReadCellsOrDefault(root, "#address-cells", 2, &cells);
    if (status != OS_EOK) {
        return status;
    }

    if (cells == 1 && source->AddressLimit > UINT32_MAX) {
        source->AddressLimit = UINT32_MAX;
    } else if (cells != 1 && cells != 2) {
        source->Status = OS_ENOTSUPPORTED;
    }
    return OS_EOK;
}

/**
 * Walks upward from device to the root node to find its DMA cache policy and
 * highest reachable physical address. Every intermediate parent bus must be a
 * simple-bus with a supported dma-ranges window. Unsupported bus features are
 * saved on source->Status so that device is blocked while the rest of the
 * system still discovers cleanly. Platform devices are leaves with no
 * dma-ranges of their own. PCI host bridges carry their own dma-ranges.
 */
static oserr_t
__ReadDmaProperties(
    _In_    const DeviceTreeNode_t* device,
    _In_    bool                    pci,
    _InOut_ ArmSMMUSource_t* source)
{
    const DeviceTreeNode_t* node;
    bool                    cacheKnown = false;

    source->AddressLimit = UINT64_MAX;
    source->CachePolicy = SHMDeviceCacheNonCoherent;
    for (node = device; node != NULL; node = node->Parent) {
        oserr_t status;

        status = __ReadCacheFlags(node, source, &cacheKnown);
        if (status != OS_EOK) {
            return status;
        }

        if (__HasUnsupportedDmaRedirect(device, node)) {
            source->Status = OS_ENOTSUPPORTED;
        }

        if (node->Parent == NULL) {
            status = __ReadRootAddressWidth(node, source);
            if (status != OS_EOK) {
                return status;
            }
            continue;
        }

        if (node == device && !pci) {
            continue;
        }

        if (!(node == device && pci) && !DeviceTreeIsCompatible(node, "simple-bus")) {
            source->Status = OS_ENOTSUPPORTED;
        }

        status = __ReadDmaWindow(node, node == device && pci, &source->AddressLimit);
        if (status != OS_EOK && status != OS_ENOTSUPPORTED) {
            return status;
        } else if (status != OS_EOK) {
            source->Status = status;
        }
    }
    return OS_EOK;
}

/**
 * Resolves the target SMMU and stream mask from one iommus or iommu-map entry.
 * arguments holds the cells numbers following the target phandle in DT.
 * When the target SMMU is missing, unsupported, or expands to more than
 * DMA_MANAGER_MAX_ATTACHMENTS IDs, range->Status stays OS_ENOTSUPPORTED so the
 * device route is blocked safely.
 */
static oserr_t
__ReadMapTarget(
    _In_    const struct __SmmuTreeParser* parser,
    _In_    const DeviceTreeNode_t*        target,
    _In_    const uint32_t*                arguments,
    _In_    uint32_t                       cells,
    _InOut_ ArmSMMURouteRange_t*           range)
{
    if (!DeviceTreeIsEnabled(target)) {
        return OS_EINVALPARAMS;
    }

    range->Status = OS_ENOTSUPPORTED;
    for (uint32_t i = 0; i < parser->UnitCount; i++) {
        const struct __SmmuTreeUnit* unit = &parser->Units[i];
        uint32_t                    count = 1;

        if (unit->Node != target) {
            continue;
        }

        if (unit->Status != OS_EOK) {
            return OS_EOK;
        }

        range->UnitIndex = unit->Index;
        range->OutputFirst = arguments[0];
        range->IgnoredBitsMask = cells == 2 ? arguments[1] : unit->Mask;
        
        // Each bit set in IgnoredBitsMask doubles how many stream IDs match.
        // Clearing bits & (bits - 1) counts set bits and stops if it overflows.
        for (uint32_t bits = range->IgnoredBitsMask; bits != 0; bits &= bits - 1) {
            count *= 2;
            if (count > DMA_MANAGER_MAX_ATTACHMENTS) {
                return OS_EOK;
            }
        }
        
        range->Status = OS_EOK;
        return OS_EOK;
    }
    return OS_EOK;
}

/**
 * Checks whether a new single-ID mapping can match any of the same incoming
 * stream IDs as an existing mapping on the same SMMU that uses a different
 * mask. Two rules overlap when every bit that neither mask ignores is equal in
 * both base IDs (OutputFirst). Different masks with overlapping IDs leave
 * hardware matching ambiguous, while identical masks describe the same device.
 */
static bool
__HasConflictingMask(
    _In_ const ArmSMMUDiscovery_t*  discovery,
    _In_ const ArmSMMURouteRange_t* range)
{
    for (uint32_t i = 0; i < discovery->RangeCount; i++) {
        const ArmSMMURouteRange_t* other = &discovery->Ranges[i];

        if (other->MapsToOneId && range->Status == OS_EOK && other->Status == OS_EOK &&
            range->UnitIndex == other->UnitIndex && range->IgnoredBitsMask != other->IgnoredBitsMask &&
            ((range->OutputFirst ^ other->OutputFirst) & ~(range->IgnoredBitsMask | other->IgnoredBitsMask)) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * Reads all entries from a platform device's iommus property. Each entry maps
 * the device onto one target ID (and optional MMU-500 mask) on an SMMU, and
 * at least one entry must be present.
 */
static oserr_t
__ReadPlatformRoutes(
    _InOut_ struct __SmmuTreeParser* parser,
    _In_    const DeviceTreeNode_t*  node,
    _InOut_ ArmSMMUSource_t*  source)
{
    ArmSMMUDiscovery_t* discovery = parser->Discovery;

    source->DeviceTreeDevice = node;
    for (uint32_t index = 0;; index++) {
        DeviceTreeReference_t reference;
        ArmSMMURouteRange_t    range = {.MapsToOneId = true};
        oserr_t               status;

        status = DeviceTreeReadReference(parser->Tree, node, "iommus", "#iommu-cells", index, &reference);
        if (status == OS_ENOENT) {
            return index == 0 ? OS_EINVALPARAMS : OS_EOK;
        } else if (status != OS_EOK) {
            return status;
        }

        if (discovery->RangeCount == ARM_SMMU_MAX_RANGES) {
            return OS_ENOTSUPPORTED;
        }

        status = __ReadMapTarget(
            parser,
            reference.Node,
            reference.Cells,
            reference.CellCount,
            &range
        );
        if (status != OS_EOK) {
            return status;
        }

        if (__HasConflictingMask(discovery, &range)) {
            return OS_EINVALPARAMS;
        }

        discovery->Ranges[discovery->RangeCount++] = range;
        source->RangeCount++;
    }
}

/**
 * Reads the PCI segment number from linux,pci-domain and confirms no other
 * enabled PCI host in the Device Tree shares that segment.
 */
static oserr_t
__ReadPciSegment(
    _In_  const struct __SmmuTreeParser* parser,
    _In_  const DeviceTreeNode_t*        node,
    _Out_ uint32_t*                      segmentOut)
{
    uint32_t segment;
    oserr_t  status;

    status = __ReadCell(node, "linux,pci-domain", &segment);
    if (status != OS_EOK) {
        // Require an explicit segment so PCI lookups match deviced reliably.
        return status == OS_ENOENT ? OS_ENOTSUPPORTED : status;
    }

    for (const DeviceTreeNode_t* other = parser->Tree->Nodes; other != NULL; other = other->NextNode) {
        const char* type = DeviceTreeReadString(other, "device_type");
        uint32_t    otherSegment;

        if (other == node || !DeviceTreeIsEnabled(other) || type == NULL || strcmp(type, "pci")) {
            continue;
        }

        // Check untranslated PCI hosts too so no two hosts ever share the same segment.
        status = __ReadCell(other, "linux,pci-domain", &otherSegment);
        if (status == OS_ENOENT) {
            continue;
        } else if (status != OS_EOK) {
            return status;
        }

        if (otherSegment == segment) {
            return OS_EINVALPARAMS;
        }
    }

    *segmentOut = segment;
    return OS_EOK;
}

/**
 * Converts a PCI host's bus-range property into the inclusive 16-bit
 * requester ID span [FirstRequesterId, LastRequesterId]. A PCI requester ID
 * puts the 8-bit bus number in bits [15:8] and device/function in bits [7:0].
 * When bus-range is absent, all 16-bit requester IDs are allowed.
 */
static oserr_t
__ReadPciBusRange(
    _In_    const DeviceTreeNode_t* node,
    _InOut_ ArmSMMUSource_t* source)
{
    const DeviceTreeProperty_t* buses = DeviceTreeGetProperty(node, "bus-range");
    const uint8_t*              bytes;
    uint32_t                    first;
    uint32_t                    last;

    source->FirstRequesterId = 0;
    source->LastRequesterId = UINT16_MAX;
    if (buses == NULL) {
        return OS_EOK;
    }

    if (buses->Length != 8) {
        return OS_EINVALPARAMS;
    }

    bytes = buses->Value;
    first = FdtReadBe32(bytes);
    last = FdtReadBe32(bytes + 4);
    if (first > last || last > UINT8_MAX) {
        return OS_EINVALPARAMS;
    }

    source->FirstRequesterId = (uint16_t)(first << 8);
    source->LastRequesterId = (uint16_t)((last << 8) | UINT8_MAX);
    return OS_EOK;
}

/**
 * Parses one entry of a PCI iommu-map property:
 *   <rid-base phandle iommu-specifier[cells] length>
 * Because the target SMMU sets #iommu-cells, entrySize receives the total byte
 * length ((cells + 3) * 4) so the caller can step to the next entry.
 */
static oserr_t
__ReadPciMapEntry(
    _In_  const struct __SmmuTreeParser* parser,
    _In_  const uint8_t*                 tuple,
    _In_  uint32_t                       available,
    _Out_ ArmSMMURouteRange_t*           rangeOut,
    _Out_ uint32_t*                      entrySize)
{
    const DeviceTreeNode_t* target;
    ArmSMMURouteRange_t     range = {0};
    uint32_t                arguments[16];
    uint32_t                cells;
    uint32_t                length;
    oserr_t                 status;

    if (available < 8) {
        return OS_EINVALPARAMS;
    }

    target = DeviceTreeFindPhandle(parser->Tree, FdtReadBe32(tuple + 4));
    if (target == NULL) {
        return OS_EINVALPARAMS;
    }

    status = __ReadCell(target, "#iommu-cells", &cells);
    if (status != OS_EOK) {
        return status == OS_ENOENT ? OS_EINVALPARAMS : status;
    }

    if (cells > 16) {
        return OS_ENOTSUPPORTED;
    }

    // Total entry size is 1 (rid-base) + 1 (phandle) + cells + 1 (length) words.
    if (available < (cells + 3) * 4) {
        return OS_EINVALPARAMS;
    }

    for (uint32_t i = 0; i < cells; i++) {
        arguments[i] = FdtReadBe32(tuple + 8 + i * 4);
    }

    range.InputFirst = FdtReadBe32(tuple);
    length = FdtReadBe32(tuple + (cells + 2) * 4);
    if (length == 0 || range.InputFirst > UINT16_MAX || length - 1 > UINT16_MAX - range.InputFirst) {
        return OS_EINVALPARAMS;
    }

    range.InputLast = range.InputFirst + length - 1;

    status = __ReadMapTarget(parser, target, arguments, cells, &range);
    if (status != OS_EOK) {
        return status;
    }

    // Verify the highest translated SMMU ID also fits inside 32 bits.
    if (range.Status == OS_EOK && length - 1 > UINT32_MAX - range.OutputFirst) {
        return OS_EINVALPARAMS;
    }

    *rangeOut = range;
    *entrySize = (cells + 3) * 4;
    return OS_EOK;
}

// Appends range to a PCI host after verifying it does not overlap any range
// already recorded for that host, so each requester ID maps unambiguously.
static oserr_t
__AddPciRange(
    _In_ ArmSMMUDiscovery_t*        discovery,
    _In_ ArmSMMUSource_t*    source,
    _In_ const ArmSMMURouteRange_t* range)
{
    for (uint32_t i = 0; i < source->RangeCount; i++) {
        const ArmSMMURouteRange_t* other = &discovery->Ranges[source->FirstRange + i];

        if (range->InputFirst <= other->InputLast && other->InputFirst <= range->InputLast) {
            return OS_EINVALPARAMS;
        }
    }

    if (discovery->RangeCount == ARM_SMMU_MAX_RANGES) {
        return OS_ENOTSUPPORTED;
    }

    discovery->Ranges[discovery->RangeCount++] = *range;
    source->RangeCount++;
    return OS_EOK;
}

/**
 * Reads the iommu-map of a PCI host bridge. Before matching against iommu-map,
 * lookups apply iommu-map-mask (default 0xffff) to the 16-bit PCI requester ID,
 * for example to collapse all functions on a device onto one stream ID.
 */
static oserr_t
__ReadPciRoutes(
    _InOut_ struct __SmmuTreeParser* parser,
    _In_    const DeviceTreeNode_t*  node,
    _InOut_ ArmSMMUSource_t*  source)
{
    const DeviceTreeProperty_t* map = DeviceTreeGetProperty(node, "iommu-map");
    uint32_t                    offset = 0;
    oserr_t                     status;

    status = __ReadPciSegment(parser, node, &source->Segment);
    if (status != OS_EOK) {
        return status;
    }

    status = __ReadPciBusRange(node, source);
    if (status != OS_EOK) {
        return status;
    }

    source->RequesterIdMask = UINT16_MAX;
    status = __ReadCell(node, "iommu-map-mask", &source->RequesterIdMask);
    if (status != OS_EOK && status != OS_ENOENT) {
        return status;
    }

    if (source->RequesterIdMask > UINT16_MAX || map->Length == 0 || map->Length % 4 != 0) {
        return OS_EINVALPARAMS;
    }

    while (offset < map->Length) {
        ArmSMMURouteRange_t range;
        uint32_t            entrySize;

        status = __ReadPciMapEntry(
            parser,
            (const uint8_t*)map->Value + offset,
            map->Length - offset,
            &range,
            &entrySize
        );
        if (status != OS_EOK) {
            return status;
        }

        status = __AddPciRange(parser->Discovery, source, &range);
        if (status != OS_EOK) {
            return status;
        }
        offset += entrySize;
    }
    return OS_EOK;
}

/**
 * Parses DMA routing for one enabled Device Tree node if it carries iommu-map
 * or iommus. Nodes with iommu-map must be PCI host bridges and may not also
 * list iommus. Stops if any map entry or bus DMA property is malformed.
 */
static oserr_t
__ReadNodeRoute(
    _InOut_ struct __SmmuTreeParser* parser,
    _In_    const DeviceTreeNode_t*  node)
{
    const DeviceTreeProperty_t* map = DeviceTreeGetProperty(node, "iommu-map");
    const DeviceTreeProperty_t* iommus = DeviceTreeGetProperty(node, "iommus");
    ArmSMMUSource_t*     source;
    const char*                 type;
    oserr_t                     status;

    if (!DeviceTreeIsEnabled(node) || (map == NULL && iommus == NULL)) {
        return OS_EOK;
    }

    // __CollectUnits already capped total tree nodes at ARM_SMMU_MAX_NODES, so
    // parser->Discovery->Routes cannot overflow here.
    source = &parser->Discovery->Routes[parser->Discovery->RouteCount++];
    source->FirstRange = parser->Discovery->RangeCount;

    type = DeviceTreeReadString(node, "device_type");
    if (map != NULL) {
        if (iommus != NULL || type == NULL || strcmp(type, "pci")) {
            return OS_ENOTSUPPORTED;
        }
        status = __ReadPciRoutes(parser, node, source);
    } else {
        status = __ReadPlatformRoutes(parser, node, source);
    }
    if (status != OS_EOK) {
        return status;
    }
    return __ReadDmaProperties(node, map != NULL, source);
}

oserr_t
ArmSMMUDiscoverDeviceTree(
    _In_    const DeviceTree_t*  tree,
    _InOut_ ArmSMMUDiscovery_t** discoveryOut)
{
    struct __SmmuTreeParser parser = { .Tree = tree };
    const DeviceTreeNode_t* node = NULL;
    oserr_t                 status;

    if (tree == NULL || tree->Root == NULL || discoveryOut == NULL) {
        return OS_EINVALPARAMS;
    }

    if (*discoveryOut != NULL) {
        return OS_EBUSY;
    }
    
    parser.Discovery = kmalloc(sizeof(ArmSMMUDiscovery_t));
    if (parser.Discovery == NULL) {
        return OS_EOOM;
    }
    
    memset(parser.Discovery, 0, sizeof(ArmSMMUDiscovery_t));
    parser.Discovery->DeviceTree = tree;
    
    // Pass 1: collect all SMMU units first so device phandles resolve
    // regardless of where SMMU nodes sit in the tree.
    status = __CollectUnits(&parser);
    if (status != OS_EOK) {
        goto failed;
    }

    // Pass 2: validate and copy every PCI host bridge and platform device route
    // before handing the discovery table back to the caller.
    for (node = tree->Nodes; node != NULL; node = node->NextNode) {
        status = __ReadNodeRoute(&parser, node);
        if (status != OS_EOK) {
            goto failed;
        }
    }

    *discoveryOut = parser.Discovery;
    return OS_EOK;

failed:
    // Normalize integer-overflow errors from DT readers into invalid-parameter
    // status for callers.
    if (status == OS_EOVERFLOW) {
        status = OS_EINVALPARAMS;
    }

    ERROR("SMMU DT: discovery failed at node %s (%u)", node == NULL ? "inventory" : node->Name, status);
    ArmSMMUDiscoveryDestroy(&parser.Discovery);
    return status;
}
