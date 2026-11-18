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
 * Reads SMMU controller nodes (register windows, interrupt lines, and stream
 * ID formats) from a parsed Device Tree into ArmSMMUDiscovery_t without
 * touching hardware registers.
 */

#include <ddk/types/interrupt.h>
#include <debug.h>
#include <log.h>
#include <string.h>

#include "devicetree-private.h"
#include "../common/private.h"
#include "../devicetree/gic-interrupt.h"

// SMMUv3 controllers expose two back-to-back 64 KiB register pages (Page 0 and
// Page 1), totaling 128 KiB.
#define SMMU_V3_REGISTER_LENGTH 0x20000U

// SMMUv3 Device Tree nodes list up to four named interrupt lines in
// interrupt-names. Bit flags catch duplicate names and stop "combined" from
// being mixed with individual lines.
#define SMMU_V3_MAX_INTERRUPTS  4U
#define SMMU_V3_NAME_EVENTQ     0x01U
#define SMMU_V3_NAME_GERROR     0x02U
#define SMMU_V3_NAME_PRIQ       0x04U
#define SMMU_V3_NAME_CMDQ_SYNC  0x08U
#define SMMU_V3_NAME_COMBINED   0x10U

// An MMU-500 provides up to 128 hardware translation slots (context banks),
// where each slot holds one DMA address space and can carry its own fault IRQ.
#define SMMU_500_MAX_CONTEXTS   128U

/**
 * Checks that an interrupt parent is an enabled GIC (Generic Interrupt
 * Controller) supported by the kernel. Nodes carrying an interrupt-map table
 * are rejected because their interrupt numbers need extra remapping before the
 * GIC driver can use them.
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

    status = DeviceTreeReadBoolean(reference->Node, "interrupt-controller", &controller);
    if (status != OS_EOK) {
        return status;
    }

    if (!controller) {
        return OS_EINVALPARAMS;
    }

    if ((!DeviceTreeIsCompatible(reference->Node, "arm,gic-v3") &&
         !DeviceTreeIsCompatible(reference->Node, "arm,gic-400") &&
         !DeviceTreeIsCompatible(reference->Node, "arm,cortex-a15-gic")) ||
        DeviceTreeGetProperty(reference->Node, "interrupt-map") != NULL) {
        return OS_ENOTSUPPORTED;
    }
    return OS_EOK;
}

/**
 * Reads the interrupt entry at index from node and translates its GIC fields
 * into an ArmSMMUInterrupt_t. Returns OS_ENOENT when no entry exists at that
 * index. Only shared peripheral interrupts (SPIs) are accepted, since SMMU
 * interrupts go to the shared GIC rather than a single CPU core.
 */
static oserr_t
__ReadInterrupt(
    _In_  const struct ArmSMMUTreeParser* parser,
    _In_  const DeviceTreeNode_t*         node,
    _In_  uint32_t                        index,
    _Out_ ArmSMMUInterrupt_t*             interrupt)
{
    DeviceTreeReference_t reference;
    Arm64GicInterrupt_t   gicInterrupt;
    oserr_t               status;

    status = DeviceTreeReadInterrupt(parser->Tree, node, index, &reference);
    if (status != OS_EOK) {
        return status;
    }

    status = __CheckGicController(&reference);
    if (status != OS_EOK) {
        return status;
    }

    status = Arm64GicDecodeInterrupt(&reference, &gicInterrupt);
    if (status != OS_EOK) {
        return status;
    }

    if (gicInterrupt.Private) {
        return OS_ENOTSUPPORTED;
    }

    // Vali stores ARM interrupt polarity and trigger mode using the kernel's
    // ACPI-style flag bits, where TRIGGERMODE marks active-high level lines and
    // leaving it clear marks rising-edge lines.
    if (gicInterrupt.Flags != ARM64_DT_GIC_EDGE_RISING && gicInterrupt.Flags != ARM64_DT_GIC_LEVEL_HIGH) {
        return OS_ENOTSUPPORTED;
    }

    interrupt->Line = gicInterrupt.Line;
    interrupt->Flags = INTERRUPT_ACPICONFORM_PRESENT;
    if (gicInterrupt.Flags == ARM64_DT_GIC_LEVEL_HIGH) {
        interrupt->Flags |= INTERRUPT_ACPICONFORM_TRIGGERMODE;
    }
    return OS_EOK;
}

/**
 * Stores one named SMMUv3 interrupt line into description. "priq" (page-request
 * queue) and "cmdq-sync" (command sync) are accepted and ignored because our
 * driver does not use those queues yet. A "combined" entry shares one line
 * for both EventQueue and GlobalError. bitOut receives the SMMU_V3_NAME_* bit
 * so the caller can catch duplicates.
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
 * Reads an SMMUv3 node's interrupts using interrupt-names, since Device Tree
 * firmware may place those lines in any order. Each name may appear once, a
 * "combined" entry must be the only line listed, and extra unnamed entries
 * past the end of interrupt-names are rejected.
 */
static oserr_t
__ReadV3Interrupts(
    _In_  const struct ArmSMMUTreeParser* parser,
    _In_  const DeviceTreeNode_t*        node,
    _Out_ ArmSMMUDescription_t*          description)
{
    const DeviceTreeProperty_t* names = DeviceTreeGetProperty(node, "interrupt-names");
    ArmSMMUInterrupt_t          interrupt;
    uint32_t                    offset = 0;
    uint32_t                    count = 0;
    uint32_t                    seen = 0;
    oserr_t                     status;

    // Leave missing interrupt lines set to UINT32_MAX so callers can detect
    // which ones firmware actually wired.
    description->V3.EventQueue.Line = UINT32_MAX;
    description->V3.GlobalError.Line = UINT32_MAX;

    status = DeviceTreeCheckStrings(node, "interrupt-names", true);
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

        // Catch repeated names and disallow mixing "combined" with other lines.
        if ((seen & bit) != 0 || (((seen | bit) & SMMU_V3_NAME_COMBINED) != 0 && count != 1)) {
            return OS_EINVALPARAMS;
        }

        seen |= bit;
        offset += (uint32_t)strlen(name) + 1;
    }

    // Verify no trailing unnamed interrupts sit past the last name string.
    status = __ReadInterrupt(parser, node, count, &interrupt);
    if (status != OS_ENOENT) {
        return status == OS_EOK ? OS_EINVALPARAMS : status;
    }
    return OS_EOK;
}

/**
 * Reads an MMU-500 node's interrupts. #global-interrupts tells how many
 * entries at the front signal controller-wide faults. All following entries
 * map 1:1 to context banks in order, and at least one context interrupt is
 * required.
 */
static oserr_t
__Read500Interrupts(
    _In_  const struct ArmSMMUTreeParser* parser,
    _In_  const DeviceTreeNode_t*        node,
    _Out_ ArmSMMUDescription_t*          description)
{
    ArmSMMUInterrupt_t interrupt;
    uint32_t           globals;
    uint32_t           count = 0;
    oserr_t            status;

    description->Mmu500.GlobalFault.Line = UINT32_MAX;
    description->Mmu500.ConfigurationFault.Line = UINT32_MAX;
    
    status = DeviceTreeReadCell(node, "#global-interrupts", &globals);
    if (status != OS_EOK) {
        return status == OS_ENOENT ? OS_EINVALPARAMS : status;
    }

    // Standard MMU-500 bindings only define a single global fault line, so
    // reject extra global interrupts until a platform binding explains them.
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
 * Reads #iommu-cells, which sets how many 32-bit numbers follow this SMMU's
 * handle in a device's iommus or iommu-map property. SMMUv3 takes one cell for
 * the stream ID. MMU-500 takes either one cell paired with the controller's
 * stream-match-mask, or two cells providing <stream-id mask> per device.
 */
static oserr_t
__ReadUnitIdFormat(
    _In_    const DeviceTreeNode_t* node,
    _In_    enum ArmSMMUKind        kind,
    _InOut_ struct ArmSMMUTreeUnit*  unit)
{
    oserr_t status;

    status = DeviceTreeReadCell(node, "#iommu-cells", &unit->Cells);
    if (status != OS_EOK) {
        return status == OS_ENOENT ? OS_EINVALPARAMS : status;
    }
    
    if ((kind == ArmSMMUv3 && unit->Cells != 1) ||
        (kind == ArmSMMU500 && unit->Cells != 1 && unit->Cells != 2)) {
        return OS_ENOTSUPPORTED;
    }

    if (kind == ArmSMMU500 && unit->Cells == 1) {
        status = DeviceTreeReadCell(node, "stream-match-mask", &unit->Mask);
        if (status != OS_EOK && status != OS_ENOENT) {
            return status;
        }
    }
    return OS_EOK;
}

/**
 * Populates description for one SMMU controller node. Rejects vendor register
 * quirks and unsupported layouts cleanly so a generic driver never pokes at
 * incompatible hardware.
 */
static oserr_t
__ReadUnit(
    _In_    const struct ArmSMMUTreeParser* parser,
    _InOut_ struct ArmSMMUTreeUnit*         unit,
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

    // dma-coherent on the SMMU itself means the SMMU's own page-table walks
    // snoop CPU caches (independent of whether devices behind it are coherent).
    status = DeviceTreeReadBoolean(node, "dma-coherent", &description->TableCoherent);
    if (status != OS_EOK) {
        return status;
    }

    // Vendor quirks that change register layout or require secure-world calls.
    if (DeviceTreeGetProperty(node, "cavium,cn9900-broken-page1-regspace") != NULL ||
        DeviceTreeGetProperty(node, "calxeda,smmu-secure-config-access") != NULL) {
        return OS_ENOTSUPPORTED;
    }

    if (description->Kind == ArmSMMUv3) {
        return __ReadV3Interrupts(parser, node, description);
    }
    return __Read500Interrupts(parser, node, description);
}

// Verifies compatible, status, and device_type strings end with a NUL byte
// before any helper searches or compares them.
static oserr_t
__CheckNodeStrings(
    _In_ const DeviceTreeNode_t* node)
{
    oserr_t status;

    status = DeviceTreeCheckStrings(node, "compatible", true);
    if (status != OS_EOK) {
        return status;
    }

    status = DeviceTreeCheckStrings(node, "status", false);
    if (status != OS_EOK) {
        return status;
    }
    return DeviceTreeCheckStrings(node, "device_type", false);
}

/**
 * Reads the single MMIO register range (reg property) of an SMMU node.
 * Rejects empty ranges or nodes listing extra register blocks, since our
 * driver only handles the standard single-region layout.
 */
static oserr_t
__ReadUnitRegisters(
    _InOut_ struct ArmSMMUTreeUnit* unit)
{
    uint64_t unusedBase;
    uint64_t unusedLength;
    oserr_t  status;

    // Reject a second reg entry (index 1) if firmware provides one.
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
 * Checks that unit's register window does not overlap any unit collected so
 * far. unit already sits at parser->Units[UnitCount - 1].
 */
static oserr_t
__CheckUnitOverlap(
    _In_ const struct ArmSMMUTreeParser* parser,
    _In_ const struct ArmSMMUTreeUnit*   unit)
{
    uint32_t i;

    for (i = 0; i + 1 < parser->UnitCount; i++) {
        const struct ArmSMMUTreeUnit* other = &parser->Units[i];

        if (unit->RegisterBase < other->RegisterBase + other->RegisterLength &&
            other->RegisterBase < unit->RegisterBase + unit->RegisterLength) {
            return OS_EINVALPARAMS;
        }
    }
    return OS_EOK;
}

oserr_t
ArmSMMUDeviceTreeCollectUnits(
    _InOut_ struct ArmSMMUTreeParser* parser)
{
    ArmSMMUDiscovery_t*     discovery = parser->Discovery;
    const DeviceTreeNode_t* node;
    uint32_t                count = 0;

    for (node = parser->Tree->Nodes; node != NULL; node = node->NextNode) {
        ArmSMMUDescription_t   description = { 0 };
        struct ArmSMMUTreeUnit* unit;
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

        // Read and overlap-check MMIO registers even for unsupported SMMUs so
        // a broken node cannot silently alias a working controller's range.
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
