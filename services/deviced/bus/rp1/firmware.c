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

/** Tracks checks during the first device-tree walk and child collection during
 * the second walk of this RP1 controller. */
struct __FdtRp1Walk {
    const struct FdtPciHost* Host;
    const struct PciBar* Bars;
    FdtRp1DeviceFn Callback;
    void* Context;
    oserr_t Status;
    unsigned int Buses;
    uint32_t Controller;
};

static int
__IsRp1Node(
    _In_ const struct FdtResources* node)
{
    return FdtStringListContains(node->Compatible, node->CompatibleLength, "raspberrypi,rp1") ||
        (!strcmp(node->Name, "rp1") &&
         FdtStringListContains(node->Compatible, node->CompatibleLength, "simple-bus"));
}

static oserr_t
__Rp1TranslateRegister(
    _In_ const struct FdtResources* rp1,
    _In_ struct __FdtRp1Walk* walk,
    _In_ uint64_t address,
    _InOut_ struct FdtRp1Range* resource)
{
    uint32_t offset;
    uint32_t space;
    uint32_t matches = 0;
    uint64_t child;
    uint64_t target;
    uint64_t length;
    uint64_t physical = 0;
    unsigned int bar;

    // Each ranges entry stores a child address in 2 32-bit cells, a PCI address
    // in 3 cells, and a size in 2 cells, for 28 bytes total.
    if (!rp1->RangesLength || rp1->RangesLength % 28) {
        return OS_EINVALPARAMS;
    }
    for (offset = 0; offset < rp1->RangesLength; offset += 28) {
        child = FdtReadCells(rp1->Ranges + offset, 2);
        space = (FdtReadBe32(rp1->Ranges + offset + 8) >> 24) & 3;
        target = FdtReadCells(rp1->Ranges + offset + 12, 2);
        length = FdtReadCells(rp1->Ranges + offset + 20, 2);
        if (!length || length - 1 > UINT64_MAX - child ||
            length - 1 > UINT64_MAX - target || (space != 2 && space != 3)) {
            return OS_EINVALPARAMS;
        }
        if (!FdtContainsRange(child, length, address, resource->Length)) {
            continue;
        }
        if (++matches != 1 || FdtTranslatePciAddress(walk->Host, space,
                target + (address - child), resource->Length, &physical) != OS_EOK) {
            return OS_EINVALPARAMS;
        }
    }
    if (matches != 1) {
        return OS_EINVALPARAMS;
    }
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
    _In_ const struct FdtResources* node,
    _In_ const struct FdtResources* rp1,
    _In_ struct __FdtRp1Walk* walk,
    _Out_ struct FdtRp1Device* device)
{
    const uint8_t* interrupts;
    uint32_t length;
    uint32_t stride;
    uint32_t offset;
    uint32_t controller;
    uint32_t i;
    uint64_t address;
    oserr_t status;

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
        address = FdtReadCells(node->Reg + i * 16, 2);
        device->Registers[i].Length = FdtReadCells(node->Reg + i * 16 + 8, 2);
        status = __Rp1TranslateRegister(rp1, walk, address, &device->Registers[i]);
        if (status != OS_EOK) {
            return status;
        }
    }

    interrupts = node->InterruptsExtended != NULL ? node->InterruptsExtended : node->Interrupts;
    length = node->InterruptsExtended != NULL ? node->InterruptsExtendedLength : node->InterruptsLength;
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
        if (controller != rp1->Phandle) {
            return OS_ENOTSUPPORTED;
        }
        device->Interrupts[i].Controller = controller;
        device->Interrupts[i].Number = FdtReadBe32(interrupts + offset);
        device->Interrupts[i].Type = FdtReadBe32(interrupts + offset + 4);
        if (device->Interrupts[i].Number >= FDT_RP1_INTERRUPT_COUNT ||
            (device->Interrupts[i].Type != 1 && device->Interrupts[i].Type != 4)) {
            return OS_ENOTSUPPORTED;
        }
    }
    return OS_EOK;
}

static void
__VisitRp1(
    _In_ const struct FdtResources* nodes,
    _In_ int depth,
    _InOut_ void* context)
{
    struct __FdtRp1Walk* walk = context;
    const struct FdtResources* node = &nodes[depth];
    const struct FdtResources* rp1;
    const struct FdtResources* host;
    struct FdtRp1Device device = { 0 };
    enum FdtPciHostType type;
    int isBus = __IsRp1Node(node);
    int busDepth = isBus ? depth : depth - 1;

    if (node->Disabled || node->AncestorDisabled || walk->Status != OS_EOK || busDepth < 1) {
        return;
    }
    rp1 = &nodes[busDepth];
    host = &nodes[busDepth - 1];
    if (!__IsRp1Node(rp1) || !FdtPciHostType(&host->View, &type) ||
        host->NodeOffset != walk->Host->NodeOffset) {
        return;
    }
    if (rp1->Malformed || rp1->AncestorMalformed || !rp1->Phandle ||
        !rp1->IsInterruptController || rp1->InterruptCells != 2 ||
        rp1->AddressCells != 2 || rp1->SizeCells != 2 || host->AddressCells != 3) {
        walk->Status = OS_EINVALPARAMS;
        return;
    }
    if (isBus) {
        walk->Buses++;
        walk->Controller = rp1->Phandle;
        return;
    }
    // These describe parts of a device, not separate devices for deviced to add.
    if (node->Compatible == NULL || node->Reg == NULL) {
        return;
    }
    walk->Status = __Rp1ChildResources(node, rp1, walk, &device);
    if (walk->Status == OS_EOK && walk->Callback != NULL) {
        walk->Callback(&device, walk->Context);
    }
}

oserr_t
FdtEnumerateRp1Children(
    _In_ const struct FdtPciHost* host,
    _In_ const struct PciBar* bars,
    _In_ FdtRp1DeviceFn callback,
    _In_ void* context)
{
    struct __FdtRp1Walk walk = { 0 };
    struct FdtResources provider;
    oserr_t status;
    unsigned int bar;

    if (host == NULL || bars == NULL || callback == NULL) {
        return OS_EINVALPARAMS;
    }
    for (bar = 0; bar < 6; bar++) {
        if (bars[bar].Size && bars[bar].Size - 1 > UINT64_MAX - bars[bar].CpuAddress) {
            return OS_EINVALPARAMS;
        }
    }
    walk.Host = host;
    walk.Bars = bars;
    walk.Status = OS_EOK;
    status = FdtWalkResources(host->Blob, host->BlobLength, __VisitRp1, &walk);
    if (status != OS_EOK || walk.Status != OS_EOK) {
        return status != OS_EOK ? status : walk.Status;
    }
    if (walk.Buses != 1) {
        return walk.Buses ? OS_EINVALPARAMS : OS_ENOENT;
    }
    // Reject duplicate controller phandles before publishing any child.
    status = FdtFindResources(host->Blob, host->BlobLength, walk.Controller, &provider);
    if (status != OS_EOK) {
        return status;
    }
    walk.Callback = callback;
    walk.Context = context;
    return FdtWalkResources(host->Blob, host->BlobLength, __VisitRp1, &walk);
}
