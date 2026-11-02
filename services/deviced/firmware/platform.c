/**
 * Copyright 2026, Philip Meulengracht
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
 */

#include <firmware/platform.h>
#include <firmware/interrupt.h>
#include <ddk/utils.h>
#include <string.h>

static oserr_t
__ReadPlatformRegisters(
    _In_    const struct FdtResources* nodes,
    _In_    int                        depth,
    _InOut_ PlatformDevice_t*          device)
{
    const struct FdtResources* node = &nodes[depth];
    uint32_t                   stride;

    if (node->Reg == NULL) {
        return OS_EOK;
    }

    // Each address and size must fit in the 64-bit values decoded below.
    if (!node->ParentAddressCells || node->ParentAddressCells > 2) {
        ERROR("__ReadPlatformRegisters: node %u has unsupported parent address cell count %u (%u)",
            node->NodeOffset, node->ParentAddressCells, OS_ENOTSUPPORTED);
        return OS_ENOTSUPPORTED;
    }
    
    if (!node->ParentSizeCells || node->ParentSizeCells > 2) {
        ERROR("__ReadPlatformRegisters: node %u has unsupported parent size cell count %u (%u)",
            node->NodeOffset, node->ParentSizeCells, OS_ENOTSUPPORTED);
        return OS_ENOTSUPPORTED;
    }

    stride = (node->ParentAddressCells + node->ParentSizeCells) * 4;
    if (!node->RegLength || node->RegLength % stride) {
        ERROR("__ReadPlatformRegisters: node %u has invalid reg length %u (%u)",
            node->NodeOffset, node->RegLength, OS_EINVALPARAMS);
        return OS_EINVALPARAMS;
    }
    
    device->RegisterCount = node->RegLength / stride;
    if (device->RegisterCount > PLATFORM_DEVICE_MAX_REGISTERS) {
        ERROR("__ReadPlatformRegisters: node %u has %u registers, limit is %u (%u)",
            node->NodeOffset, device->RegisterCount,
            (uint32_t)PLATFORM_DEVICE_MAX_REGISTERS, OS_ENOTSUPPORTED);
        return OS_ENOTSUPPORTED;
    }

    for (unsigned int i = 0; i < device->RegisterCount; i++) {
        uint64_t base;
        uint64_t length;
        oserr_t  status = FdtRawRegister(node, i, &base, &length);
        if (status != OS_EOK) {
            ERROR("__ReadPlatformRegisters: node %u register %u could not be decoded (%u)",
                node->NodeOffset, i, status);
            return status;
        }
        
        if (!length || length - 1 > UINT64_MAX - base) {
            ERROR("__ReadPlatformRegisters: node %u register %u has invalid extent (%u)",
                node->NodeOffset, i, OS_EINVALPARAMS);
            return OS_EINVALPARAMS;
        }
        
        if (!FdtTranslateAddress(nodes, depth - 1, length, &base, 0)) {
            ERROR("__ReadPlatformRegisters: node %u register %u address could not be "
                "translated (%u)",
                node->NodeOffset, i, OS_ENOTSUPPORTED);
            return OS_ENOTSUPPORTED;
        }
        
        device->Registers[i].Base = base;
        device->Registers[i].Length = length;
    }

    return OS_EOK;
}

static oserr_t
__ReadPlatformInterrupts(
    _In_    const struct FdtTree*      tree,
    _In_    const struct FdtResources* node,
    _InOut_ PlatformDevice_t*          device)
{
    const uint8_t*      cells = node->InterruptsExtended;
    uint32_t            length = node->InterruptsExtendedLength;
    uint32_t            inherited = 0;
    uint32_t            offset = 0;
    struct FdtResources resources;
    struct FdtInterrupt interrupt;
    const uint8_t*      arguments;
    uint32_t            trigger;
    oserr_t             status;

    // interrupts-extended includes a controller reference for each entry.
    // Plain interrupts entries require the inherited interrupt-parent.
    if (cells == NULL) {
        cells = node->Interrupts;
        length = node->InterruptsLength;
        inherited = node->InterruptParent;
        if (cells != NULL && !inherited) {
            ERROR("__ReadPlatformInterrupts: node %u has interrupts without interrupt-parent (%u)",
                node->NodeOffset, OS_ENOTSUPPORTED);
            return OS_ENOTSUPPORTED;
        }
    }
    if (cells == NULL) {
        return OS_EOK;
    }
    if (!length) {
        ERROR("__ReadPlatformInterrupts: node %u has an empty interrupt list (%u)",
            node->NodeOffset, OS_EINVALPARAMS);
        return OS_EINVALPARAMS;
    }

    while (offset < length) {
        if (device->InterruptCount == PLATFORM_DEVICE_MAX_INTERRUPTS) {
            ERROR("__ReadPlatformInterrupts: node %u exceeds the %u interrupt limit (%u)",
                node->NodeOffset, (uint32_t)PLATFORM_DEVICE_MAX_INTERRUPTS, OS_ENOTSUPPORTED);
            return OS_ENOTSUPPORTED;
        }

        status = FdtNextReference(
            tree,
            cells,
            length,
            "#interrupt-cells",
            inherited,
            &offset,
            &resources,
            &arguments
        );
        if (status != OS_EOK) {
            ERROR("__ReadPlatformInterrupts: node %u has an invalid interrupt "
                "reference at byte %u (%u)",
                node->NodeOffset, offset, status);
            return status;
        }

        status = FdtGicInterrupt(&resources, arguments, &interrupt);
        if (status != OS_EOK) {
            ERROR("__ReadPlatformInterrupts: node %u interrupt provider %u is unsupported (%u)",
                node->NodeOffset, resources.NodeOffset, status);
            return status;
        }

        trigger = FdtReadBe32(arguments + 8);
        // Accept rising-edge (1) and high-level (4) interrupt flags.
        if (trigger != 1 && trigger != 4) {
            ERROR("__ReadPlatformInterrupts: node %u interrupt provider %u has "
                "unsupported trigger %u (%u)",
                node->NodeOffset, resources.NodeOffset, trigger, OS_ENOTSUPPORTED);
            return OS_ENOTSUPPORTED;
        }

        // GIC source numbers are INTIDs, with the SPI bias already applied.
        device->Interrupts[device->InterruptCount++] = (struct PlatformInterrupt) {
            .Controller = interrupt.Controller,
            .Number = (uint32_t)interrupt.Line,
            .Type = trigger
        };
    }

    return OS_EOK;
}

oserr_t
FdtDescribePlatformDevice(
    _In_  const struct FdtTree*      tree,
    _In_  const struct FdtResources* nodes,
    _In_  int                        depth,
    _Out_ PlatformDevice_t*          device)
{
    const struct FdtResources* node;
    PlatformDevice_t           description = { 0 };
    oserr_t                    status;

    if (depth <= 0) {
        ERROR("FdtDescribePlatformDevice: invalid node depth %d (%u)", depth, OS_EINVALPARAMS);
        return OS_EINVALPARAMS;
    }

    node = &nodes[depth];
    if (node->Malformed || node->ParentMalformed) {
        ERROR("FdtDescribePlatformDevice: node %u or its parent is malformed (%u)",
            node->NodeOffset, OS_EINVALPARAMS);
        return OS_EINVALPARAMS;
    }
    
    if (node->Disabled || node->ParentDisabled || node->Compatible == NULL) {
        TRACE("FdtDescribePlatformDevice: skipping node %u (disabled=%u, "
            "parent-disabled=%u, compatible=%u)",
            node->NodeOffset, node->Disabled, node->ParentDisabled,
            node->Compatible != NULL);
        return OS_ENOENT;
    }
    
    if (node->CompatibleLength > sizeof(description.Compatibles)) {
        ERROR("FdtDescribePlatformDevice: node %u compatible length %u exceeds limit %u (%u)",
            node->NodeOffset, node->CompatibleLength,
            (uint32_t)sizeof(description.Compatibles), OS_ENOTSUPPORTED);
        return OS_ENOTSUPPORTED;
    }

    description.Base.Length = sizeof(description);
    description.Version = PLATFORM_DEVICE_VERSION;
    description.FirmwareNode = node->NodeOffset;
    description.CompatibleLength = node->CompatibleLength;
    memcpy(description.Compatibles, node->Compatible, node->CompatibleLength);

    // Simple buses describe child address translation and carry identity only.
    if (!node->IsSimpleBus) {
        status = __ReadPlatformRegisters(nodes, depth, &description);
        if (status != OS_EOK) {
            return status;
        }
        
        status = __ReadPlatformInterrupts(tree, node, &description);
        if (status != OS_EOK) {
            return status;
        }
    }
    if (!PlatformDeviceValidate(&description)) {
        ERROR("FdtDescribePlatformDevice: node %u has an invalid platform device description (%u)",
            node->NodeOffset, OS_EINVALPARAMS);
        return OS_EINVALPARAMS;
    }

    // Leave the caller's output unchanged if any resource is invalid.
    *device = description;
    return OS_EOK;
}
