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

#include <firmware/rp1.h>
#include <firmware/resources.h>
#include <string.h>

/**
 * @brief Keeps the inputs and results needed while reading one RP1 controller.
 * Reads the tree twice: first to check all child descriptions and record the
 * controller, then to pass each child to the caller's callback.
 */
struct __FdtRp1Walk {
    const struct FdtPciHost* Host;
    const struct PciBar*     Bars;
    FdtRp1DeviceFn           Callback;
    void*                    Context;
    oserr_t                  Status;
    unsigned int             Buses;
    uint32_t                 Controller;
};

static int
__IsRp1Node(
    _In_ const struct FdtResources* node)
{
    // Firmware may name the controller type as "raspberrypi,rp1", or use an
    // "rp1" node with type "simple-bus", which groups devices under one bus.
    // Accept both descriptions used by different firmware versions.
    return FdtStringListContains(node->Compatible, node->CompatibleLength, "raspberrypi,rp1") ||
        (!strcmp(node->Name, "rp1") &&
         FdtStringListContains(node->Compatible, node->CompatibleLength, "simple-bus"));
}

static oserr_t
__Rp1TranslateRegister(
    _In_    const struct FdtResources* rp1,
    _In_    struct __FdtRp1Walk*       walk,
    _In_    uint64_t                   address,
    _InOut_ struct FdtRp1Range*        resource)
{
    uint32_t     offset;
    uint32_t     space;
    uint32_t     matches = 0;
    uint64_t     child;
    uint64_t     target;
    uint64_t     length;
    uint64_t     physical = 0;
    unsigned int bar;

    // Convert the child's address in two steps: RP1's "ranges" property maps it
    // to a PCI address, then the host converts that to a CPU physical address.
    // Each "ranges" entry has two 32-bit words for the child address, three for
    // the PCI address, and two for the length, so incomplete entries are invalid.
    if (!rp1->RangesLength || rp1->RangesLength % 28) {
        return OS_EINVALPARAMS;
    }
    
    for (offset = 0; offset < rp1->RangesLength; offset += 28) {
        child = FdtReadCells(rp1->Ranges + offset, 2);
        space = (FdtReadBe32(rp1->Ranges + offset + 8) >> 24) & 3;
        target = FdtReadCells(rp1->Ranges + offset + 12, 2);
        length = FdtReadCells(rp1->Ranges + offset + 20, 2);
        
        // Reject ranges with no bytes or an end address too large for 64 bits.
        // Only memory ranges are supported here; PCI I/O ports use a separate
        // kind of address space.
        if (!length || length - 1 > UINT64_MAX - child ||
            length - 1 > UINT64_MAX - target || (space != 2 && space != 3)) {
            return OS_EINVALPARAMS;
        }
        
        if (!FdtContainsRange(child, length, address, resource->Length)) {
            continue;
        }
        
        // Exactly one range must map these child registers to CPU addresses.
        // Multiple matches would make it unclear which address to use.
        if (++matches != 1 || FdtTranslatePciAddress(walk->Host, space,
                target + (address - child), resource->Length, &physical) != OS_EOK) {
            return OS_EINVALPARAMS;
        }
    }
    
    if (matches != 1) {
        return OS_EINVALPARAMS;
    }
    
    // A child may use only memory assigned to this RP1 PCI device. A Base
    // Address Register (BAR) describes an assigned range; all of the child's
    // register bytes must fit inside one such range.
    for (bar = 0; bar < 6; bar++) {
        if (walk->Bars[bar].State == PciBarAssigned &&
            (walk->Bars[bar].Space == 2 || walk->Bars[bar].Space == 3) &&
            FdtContainsRange(walk->Bars[bar].CpuAddress, walk->Bars[bar].Size,
                physical, resource->Length)) {
            resource->Base = physical;
            return OS_EOK;
        }
    }
    return OS_EINVALPARAMS;
}

static oserr_t
__Rp1ChildResources(
    _In_  const struct FdtResources* node,
    _In_  const struct FdtResources* rp1,
    _In_  struct __FdtRp1Walk*       walk,
    _Out_ struct FdtRp1Device*       device)
{
    const uint8_t* interrupts;
    uint32_t       length;
    uint32_t       stride;
    uint32_t       offset;
    uint32_t       controller;
    uint32_t       i;
    uint64_t       address;
    oserr_t        status;

    // Each register entry contains a 64-bit address and a 64-bit length. Reject
    // incomplete entries or more entries than the output array can hold.
    if (node->Malformed || node->AncestorMalformed || !node->RegLength ||
        node->RegLength % 16 || node->RegLength / 16 > FDT_RP1_MAX_REGISTERS) {
        return OS_EINVALPARAMS;
    }
    
    device->Name = node->Name;
    device->Compatible = (const char*)node->Compatible;
    device->CompatibleLength = node->CompatibleLength;
    device->NodeOffset = node->NodeOffset;
    device->Controller = rp1->Phandle;
    device->RegisterCount = node->RegLength / 16;
    
    for (i = 0; i < device->RegisterCount; i++) {
        // Child addresses are relative to RP1, so convert each one to the
        // physical address that the operating system can map.
        address = FdtReadCells(node->Reg + i * 16, 2);
        device->Registers[i].Length = FdtReadCells(node->Reg + i * 16 + 8, 2);
        
        status = __Rp1TranslateRegister(rp1, walk, address, &device->Registers[i]);
        if (status != OS_EOK) {
            return status;
        }
    }

    interrupts = node->InterruptsExtended != NULL ? node->InterruptsExtended : node->Interrupts;
    length = node->InterruptsExtended != NULL ? node->InterruptsExtendedLength : node->InterruptsLength;
    
    // An "interrupts" entry contains two 32-bit values: source number and
    // trigger type. An "interrupts-extended" entry also includes a 32-bit
    // controller ID. Check that complete entries fit before reading them.
    stride = node->InterruptsExtended != NULL ? 12 : 8;
    if ((interrupts != NULL && !length) || length % stride ||
        length / stride > FDT_RP1_MAX_INTERRUPTS) {
        return OS_EINVALPARAMS;
    }
    
    device->InterruptCount = length / stride;
    for (i = 0; i < device->InterruptCount; i++) {
        offset = i * stride;
        controller = node->InterruptParent;
        if (node->InterruptsExtended != NULL) {
            controller = FdtReadBe32(interrupts + offset);
            offset += 4;
        }
        
        // This code can route only interrupts raised by the RP1 controller.
        if (controller != rp1->Phandle) {
            return OS_ENOTSUPPORTED;
        }
        
        device->Interrupts[i].Controller = controller;
        device->Interrupts[i].Number = FdtReadBe32(interrupts + offset);
        device->Interrupts[i].Type = FdtReadBe32(interrupts + offset + 4);
        
        // Accept source numbers 0 through 60 and the two supported signal types:
        // 1 means a change from low to high; 4 means the signal stays high.
        if (device->Interrupts[i].Number >= FDT_RP1_INTERRUPT_COUNT ||
            (device->Interrupts[i].Type != 1 && device->Interrupts[i].Type != 4)) {
            return OS_ENOTSUPPORTED;
        }
    }
    return OS_EOK;
}

static void
__VisitRp1(
    _In_    const struct FdtResources* nodes,
    _In_    int                        depth,
    _InOut_ void*                      context)
{
    struct __FdtRp1Walk*       walk = context;
    const struct FdtResources* node = &nodes[depth];
    const struct FdtResources* rp1;
    const struct FdtResources* host;
    struct FdtRp1Device        device = { 0 };
    enum FdtPciHostType        type;
    int                        isBus = __IsRp1Node(node);
    int                        busDepth = isBus ? depth : depth - 1;

    // Skip hardware that firmware marks as disabled. Once a child description
    // fails its checks, keep that error and stop examining more children.
    if (node->Disabled || node->AncestorDisabled || walk->Status != OS_EOK || busDepth < 1) {
        return;
    }

    rp1 = &nodes[busDepth];
    host = &nodes[busDepth - 1];
    
    // The firmware tree can contain other PCI controllers and RP1 devices. Only
    // process nodes below the RP1 controller attached to the requested host.
    if (!__IsRp1Node(rp1) || !FdtPciHostType(&host->View, &type) ||
        host->NodeOffset != walk->Host->NodeOffset) {
        return;
    }
    
    // A device-tree cell is a 32-bit value. These counts describe how many
    // values make up each address, size, and interrupt entry. Different counts
    // would make us read the child data at the wrong positions.
    if (rp1->Malformed || rp1->AncestorMalformed || !rp1->Phandle ||
        !rp1->IsInterruptController || rp1->InterruptCells != 2 ||
        rp1->AddressCells != 2 || rp1->SizeCells != 2 || host->AddressCells != 3) {
        walk->Status = OS_EINVALPARAMS;
        return;
    }
    
    if (isBus) {
        // Count the RP1 controller; only its children are passed to the callback.
        walk->Buses++;
        walk->Controller = rp1->Phandle;
        return;
    }
    
    // Only return devices with a hardware type ("compatible") and register
    // ranges ("reg"). Other nodes may describe internal parts of a device.
    if (node->Compatible == NULL || node->Reg == NULL) {
        return;
    }
    
    walk->Status = __Rp1ChildResources(node, rp1, walk, &device);
    if (walk->Status == OS_EOK && walk->Callback != NULL) {
        // Callers receive children only on the second read, after every child
        // has passed its checks. A bad description later in the tree therefore
        // cannot leave the caller with an incomplete list.
        walk->Callback(&device, walk->Context);
    }
}

oserr_t
FdtEnumerateRp1Children(
    _In_ const struct FdtPciHost* host,
    _In_ const struct PciBar*     bars,
    _In_ FdtRp1DeviceFn           callback,
    _In_ void*                    context)
{
    struct __FdtRp1Walk walk = { 0 };
    struct FdtResources provider;
    oserr_t             status;
    unsigned int        bar;

    if (host == NULL || bars == NULL || callback == NULL) {
        return OS_EINVALPARAMS;
    }
    
    // Check that the last byte of every PCI address range fits in a 64-bit
    // address. Later checks rely on those end addresses being valid.
    for (bar = 0; bar < 6; bar++) {
        if (bars[bar].Size && bars[bar].Size - 1 > UINT64_MAX - bars[bar].CpuAddress) {
            return OS_EINVALPARAMS;
        }
    }

    walk.Host = host;
    walk.Bars = bars;
    walk.Status = OS_EOK;

    // Check every RP1 child's description and convert its register addresses
    // before calling the caller's callback. An invalid entry must not leave
    // the caller with only part of the device list.
    status = FdtWalkResources(host->Blob, host->BlobLength, __VisitRp1, &walk);
    if (status != OS_EOK || walk.Status != OS_EOK) {
        return status != OS_EOK ? status : walk.Status;
    }

    if (walk.Buses != 1) {
        // There must be one RP1 controller described under this PCI host.
        // With more than one, we cannot tell which description to use.
        return walk.Buses ? OS_EINVALPARAMS : OS_ENOENT;
    }

    // Confirm the controller ID names one valid firmware node before using it
    // in the child interrupt descriptions returned during the second read.
    status = FdtFindResources(
        host->Blob,
        host->BlobLength,
        walk.Controller,
        &provider
    );
    if (status != OS_EOK) {
        return status;
    }

    // All checks passed; read the tree again to pass each child to the callback.
    walk.Callback = callback;
    walk.Context = context;
    return FdtWalkResources(host->Blob, host->BlobLength, __VisitRp1, &walk);
}
