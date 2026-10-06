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

#include "pci.h"
#include "resources.h"
#include "bcm.h"
#include <ddk/utils.h>
#include <stdlib.h>
#include <string.h>

/** PCI binding properties, decoded after host matching. */
struct __PciNode {
    const uint8_t* BusRange;
    uint32_t BusRangeLength;
    int HasDomain;
    uint32_t Domain;
    const uint8_t* InterruptMap;
    uint32_t InterruptMapLength;
    const uint8_t* InterruptMask;
    uint32_t InterruptMaskLength;
};

/** Explicit domains are reserved before any automatic assignment or callback. */
struct __PciWalk {
    const void* Blob;
    size_t Length;
    uint32_t* ReservedDomains;
    size_t ReservedCount;
    uint64_t NextDomain;
    oserr_t Status;
    FdtPciHostFn Callback;
    void* Context;
};

static oserr_t
__PciNextDomain(
    _InOut_ struct __PciWalk* walk,
    _Out_ uint32_t* domainOut)
{
    size_t index;

    // Restart the search after skipping a reservation: firmware order need
    // not match numeric order. Earlier automatic assignments are below NextDomain.
    index = 0;
    while (index < walk->ReservedCount) {
        if (walk->NextDomain == walk->ReservedDomains[index]) {
            walk->NextDomain++;
            index = 0;
        } else {
            index++;
        }
    }
    if (walk->NextDomain > UINT32_MAX) {
        return OS_EOVERFLOW;
    }
    *domainOut = (uint32_t)walk->NextDomain++;
    return OS_EOK;
}

int
FdtPciHostType(
    _In_ const struct FdtNode* node,
    _Out_ enum FdtPciHostType* type)
{
    if (FdtBcmHostType(node, type)) {
        return 1;
    }
    if (FdtCompatible(node, "pci-host-ecam-generic")) {
        *type = FdtPciHostEcam;
        return 1;
    }
    return 0;
}

static void
__ClassifyDmaWindows(
    _InOut_ struct FdtPciHost* host);

static void
__EmitPciHost(
        _In_ const struct FdtResources* nodes,
        _In_ int                    depth,
        _In_ const struct FdtResources* node,
        _InOut_ struct __PciWalk*    walk)
{
    struct __PciNode pci = { 0 };
    oserr_t status;
    const struct FdtResources* parent = &nodes[depth - 1];
    struct FdtPciHost host = { 0 };
    uint32_t          addressCells = parent->AddressCells;
    uint32_t          busStart     = 0;
    uint32_t          busEnd       = 255;
    uint64_t          busesDecoded;
    uint32_t          cells;
    uint32_t          offset;
    struct FdtPciWindow* window;

    if (node->Malformed || node->AncestorMalformed || node->RegisterStatus != OS_EOK) {
        WARNING("FdtEnumeratePciHosts unsupported reg layout for ECAM host");
        return;
    }

    host.NodeOffset = node->NodeOffset;
    host.EcamBase   = node->PhysicalBase;
    host.EcamLength = node->PhysicalLength;
    FdtPciHostType(&node->View, &host.Type);
    if (FdtBcmHostPolicy(&node->View, &host) != OS_EOK) {
        return;
    }
    pci.BusRange = FdtProperty(&node->View, "bus-range", &pci.BusRangeLength);
    pci.InterruptMap = FdtProperty(&node->View, "interrupt-map", &pci.InterruptMapLength);
    pci.InterruptMask = FdtProperty(&node->View, "interrupt-map-mask", &pci.InterruptMaskLength);
    status = FdtScalar(&node->View, "linux,pci-domain", &pci.Domain);
    if (status != OS_EOK && status != OS_ENOENT) {
        return;
    }
    pci.HasDomain = status == OS_EOK;
    host.Phandle = node->Phandle;
    host.MsiParent = node->MsiParent;
    host.MsiParentLength = node->MsiParentLength;
    host.InterruptParent = node->InterruptParent;
    host.Interrupts = node->Interrupts;
    host.InterruptsLength = node->InterruptsLength;
    host.InterruptsExtended = node->InterruptsExtended;
    host.InterruptsExtendedLength = node->InterruptsExtendedLength;
    host.InterruptNames = node->InterruptNames;
    host.InterruptNamesLength = node->InterruptNamesLength;
    host.Blob = walk->Blob;
    host.BlobLength = walk->Length;
    host.InterruptMap = pci.InterruptMap;
    host.InterruptMapLength = pci.InterruptMapLength;
    host.InterruptMask = pci.InterruptMask;
    host.InterruptMaskLength = pci.InterruptMaskLength;
    host.Resets = node->Resets;
    host.ResetsLength = node->ResetsLength;
    host.ResetNames = node->ResetNames;
    host.ResetNamesLength = node->ResetNamesLength;
    host.Clocks = node->Clocks;
    host.ClocksLength = node->ClocksLength;
    host.ClockNames = node->ClockNames;
    host.ClockNamesLength = node->ClockNamesLength;
    if (node->RangesLength != 0) {
        if (node->AddressCells != 3 || node->SizeCells == 0 || node->SizeCells > 2) {
            return;
        }
        cells = 3 + addressCells + node->SizeCells;
        if (node->RangesLength % (cells * 4) != 0 ||
            node->RangesLength / (cells * 4) > 16) {
            return;
        }
        for (offset = 0; offset < node->RangesLength; offset += cells * 4) {
            window = &host.Windows[host.WindowCount++];
            window->Attributes = FdtReadBe32(node->Ranges + offset);
            window->Space = (window->Attributes >> 24) & 3;
            window->BusBase = FdtReadCells(node->Ranges + offset + 4, 2);
            window->PhysicalBase = FdtReadCells(node->Ranges + offset + 12, addressCells);
            window->Length = FdtReadCells(node->Ranges + offset + (3 + addressCells) * 4,
                    node->SizeCells);
            if (window->Length == 0 || window->Length - 1 > UINT64_MAX - window->BusBase ||
                !FdtTranslateAddress(nodes, depth - 1, window->Length, &window->PhysicalBase, 0)) {
                return;
            }
        }
    }

    if (node->DmaRangesLength != 0) {
        if (node->AddressCells != 3 || node->SizeCells == 0 || node->SizeCells > 2) {
            return;
        }
        cells = 3 + addressCells + node->SizeCells;
        if (node->DmaRangesLength % (cells * 4) != 0 ||
            node->DmaRangesLength / (cells * 4) > 16) {
            return;
        }
        for (offset = 0; offset < node->DmaRangesLength; offset += cells * 4) {
            window = &host.DmaWindows[host.DmaWindowCount++];
            window->Attributes = FdtReadBe32(node->DmaRanges + offset);
            window->Space = (window->Attributes >> 24) & 3;
            window->BusBase = FdtReadCells(node->DmaRanges + offset + 4, 2);
            window->PhysicalBase = FdtReadCells(node->DmaRanges + offset + 12, addressCells);
            window->Length = FdtReadCells(node->DmaRanges + offset + (3 + addressCells) * 4,
                    node->SizeCells);
            if (window->Length == 0 || window->Length - 1 > UINT64_MAX - window->BusBase ||
                !FdtTranslateAddress(nodes, depth - 1, window->Length, &window->PhysicalBase, 1)) {
                return;
            }
        }
    }

    if (pci.BusRange != NULL && pci.BusRangeLength != 8) {
        return;
    }
    if (pci.BusRangeLength == 8) {
        busStart = FdtReadBe32(pci.BusRange);
        busEnd   = FdtReadBe32(pci.BusRange + 4);
    }
    if (busStart > 255 || busEnd > 255 || busEnd < busStart) {
        WARNING("FdtEnumeratePciHosts invalid bus-range %u-%u", busStart, busEnd);
        return;
    }

    // Only scan buses that the ECAM window actually decodes, 1MB per bus.
    busesDecoded = host.Type == FdtPciHostEcam ? host.EcamLength >> 20 : 256;
    if (busesDecoded == 0) {
        return;
    }
    if ((uint64_t)(busEnd - busStart) + 1 > busesDecoded) {
        busEnd = busStart + (uint32_t)busesDecoded - 1;
    }

    host.BusStart = (uint8_t)busStart;
    host.BusEnd   = (uint8_t)busEnd;
    if (pci.HasDomain) {
        host.Segment = pci.Domain;
    } else {
        walk->Status = __PciNextDomain(walk, &host.Segment);
        if (walk->Status != OS_EOK) {
            return;
        }
    }
    __ClassifyDmaWindows(&host);
    walk->Callback(&host, walk->Context);
}
static void
__ClassifyMemory(
        _In_ const struct FdtResources* nodes,
        _In_ int depth,
        _InOut_ void* context)
{
    struct FdtPciHost* host = context;
    const struct FdtResources* node = &nodes[depth];
    uint32_t ac;
    uint32_t sc;
    uint32_t stride;
    uint64_t base;
    uint64_t length;
    struct FdtPciWindow* window;

    if (node->Disabled || node->AncestorDisabled || !node->IsMemory || node->Malformed || node->AncestorMalformed || depth == 0) {
        return;
    }
    ac = nodes[depth - 1].AddressCells;
    sc = nodes[depth - 1].SizeCells;
    stride = (ac + sc) * 4;
    if (!ac || ac > 2 || !sc || sc > 2 || node->RegLength % stride) {
        return;
    }
    for (uint32_t offset = 0; offset < node->RegLength; offset += stride) {
        base = FdtReadCells(node->Reg + offset, ac);
        length = FdtReadCells(node->Reg + offset + ac * 4, sc);
        if (!FdtTranslateAddress(nodes, depth - 1, length, &base, 0)) {
            continue;
        }
        for (uint32_t i = 0; i < host->DmaWindowCount; i++) {
            window = &host->DmaWindows[i];
            if (window->Kind == FdtDmaWindowUnknown &&
                (window->Space == 2 || window->Space == 3) &&
                (FdtContainsRange(window->PhysicalBase, window->Length, base, length) ||
                 FdtContainsRange(base, length, window->PhysicalBase, window->Length))) {
                window->Kind = FdtDmaWindowRam;
            }
        }
    }
}

static void
__ClassifyDmaWindows(
        _InOut_ struct FdtPciHost* host)
{
    struct FdtPciMsi msi;
    struct FdtPciWindow* window;
    int hasMip = FdtResolvePciMsi(host, &msi) == OS_EOK && msi.IsMip;

    for (uint32_t i = 0; i < host->DmaWindowCount; i++) {
        window = &host->DmaWindows[i];
        if (hasMip && FdtContainsRange(window->BusBase, window->Length,
                msi.DoorbellBase, msi.DoorbellLength) &&
            msi.RegisterBase >= window->PhysicalBase &&
            msi.RegisterBase - window->PhysicalBase == msi.DoorbellBase - window->BusBase) {
            window->Kind = FdtDmaWindowMsi;
            continue;
        }
        for (uint32_t j = 0; j < host->WindowCount; j++) {
            if (FdtContainsRange(host->Windows[j].PhysicalBase, host->Windows[j].Length,
                    window->PhysicalBase, window->Length)) {
                window->Kind = FdtDmaWindowPeer;
                break;
            }
        }
    }
    // RAM apertures can be larger than installed memory. Require a firmware
    // memory bank as evidence, never infer RAM from entry order or address zero.
    FdtWalkResources(host->Blob, host->BlobLength, __ClassifyMemory, host);
}

oserr_t
FdtTranslatePciAddress(
        _In_ const struct FdtPciHost* host,
        _In_ uint32_t                space,
        _In_ uint64_t                address,
        _In_ uint64_t                length,
        _Out_ uint64_t*              physicalOut)
{
    const struct FdtPciWindow* window;
    uint32_t                  index;
    uint64_t                  displacement;

    if (host == NULL || physicalOut == NULL || host->WindowCount > 16 ||
        length == 0 || length - 1 > UINT64_MAX - address) {
        return OS_EINVALPARAMS;
    }
    for (index = 0; index < host->WindowCount; index++) {
        window = &host->Windows[index];
        if ((window->Space != space &&
             !((space == 2 || space == 3) && (window->Space == 2 || window->Space == 3))) ||
            address < window->BusBase) {
            continue;
        }
        displacement = address - window->BusBase;
        if (displacement < window->Length && length <= window->Length - displacement) {
            if (displacement > UINT64_MAX - window->PhysicalBase ||
                length - 1 > UINT64_MAX - window->PhysicalBase - displacement) {
                return OS_EINVALPARAMS;
            }
            *physicalOut = window->PhysicalBase + displacement;
            return OS_EOK;
        }
    }
    return OS_ENOENT;
}

oserr_t
FdtResolvePciInterrupt(
        _In_ const struct FdtPciHost* host,
        _In_ unsigned int            bus,
        _In_ unsigned int            slot,
        _In_ unsigned int            function,
        _In_ unsigned int            pin,
        _Out_ int*                   lineOut,
        _Out_ unsigned int*          flagsOut)
{
    struct FdtResources controller;
    struct FdtInterrupt interrupt;
    uint32_t        key[4];
    uint32_t        offset = 0;
    uint32_t        stride;
    uint32_t        index;
    uint32_t        mask;
    const uint8_t*  entry;
    const uint8_t*  specifier;
    int             matches;
    oserr_t         status;

    if (host == NULL || lineOut == NULL || flagsOut == NULL ||
        bus > 255 || slot > 31 || function > 7 || pin == 0 || pin > 4) {
        return OS_EINVALPARAMS;
    }
    if (host->InterruptMap == NULL) {
        return OS_ENOENT;
    }
    if ((host->InterruptMapLength & 3) ||
        (host->InterruptMask != NULL && host->InterruptMaskLength != 16)) {
        return OS_EINVALPARAMS;
    }
    key[0] = (bus << 16) | (slot << 11) | (function << 8);
    key[1] = 0;
    key[2] = 0;
    key[3] = pin;
    while (offset < host->InterruptMapLength) {
        if (host->InterruptMapLength - offset < 20) {
            return OS_EINVALPARAMS;
        }
        entry = host->InterruptMap + offset;
        memset(&controller, 0, sizeof(controller));
        status = FdtFindResources(host->Blob, host->BlobLength, FdtReadBe32(entry + 16), &controller);
        if (status != OS_EOK) {
            return status;
        }
        if (!controller.HasAddressCells) {
            controller.AddressCells = 0;
        }
        if (controller.AddressCells > 2 || controller.InterruptCells == 0 ||
            controller.InterruptCells > 16) {
            return OS_ENOTSUPPORTED;
        }
        stride = (5 + controller.AddressCells + controller.InterruptCells) * 4;
        if (stride > host->InterruptMapLength - offset) {
            return OS_EINVALPARAMS;
        }
        matches = 1;
        for (index = 0; index < 4; index++) {
            mask = host->InterruptMask == NULL ? UINT32_MAX : FdtReadBe32(host->InterruptMask + index * 4);
            if ((key[index] & mask) != (FdtReadBe32(entry + index * 4) & mask)) {
                matches = 0;
            }
        }
        if (matches) {
            specifier = entry + (5 + controller.AddressCells) * 4;
            status = FdtGicInterrupt(&controller, specifier, &interrupt);
            if (status != OS_EOK) {
                return status;
            }
            *lineOut = interrupt.Line;
            *flagsOut = interrupt.Flags;
            return OS_EOK;
        }
        offset += stride;
    }
    return OS_ENOENT;
}

oserr_t
FdtResolvePciNamedInterrupt(
        _In_ const struct FdtPciHost* host,
        _In_ const char* name,
        _Out_ struct FdtInterrupt* interrupt)
{
    struct FdtResources provider;
    struct FdtInterrupt result = { 0 };
    const uint8_t* cells;
    uint32_t length;
    uint32_t selected;
    uint32_t names;
    uint32_t index = 0;
    uint32_t offset = 0;
    uint32_t phandle;
    const uint8_t* arguments;
    oserr_t status;

    if (host == NULL || name == NULL || interrupt == NULL) {
        return OS_EINVALPARAMS;
    }
    status = FdtNameIndex(host->InterruptNames, host->InterruptNamesLength, name, &selected, &names);
    if (status != OS_EOK) {
        return status;
    }
    cells = host->InterruptsExtended != NULL ? host->InterruptsExtended : host->Interrupts;
    length = host->InterruptsExtended != NULL ? host->InterruptsExtendedLength : host->InterruptsLength;
    if (cells == NULL || (length & 3)) {
        return OS_EINVALPARAMS;
    }
    while (offset < length) {
        phandle = host->InterruptsExtended != NULL ? 0 : host->InterruptParent;
        if (host->InterruptsExtended == NULL && !phandle) {
            return OS_EINVALPARAMS;
        }
        status = FdtNextReference(host->Blob, host->BlobLength, cells, length,
            "#interrupt-cells", phandle, &offset, &provider, &arguments);
        if (status != OS_EOK) {
            return status;
        }
        if (!provider.InterruptCells) {
            return OS_EINVALPARAMS;
        }
        if (index == selected) {
            status = FdtGicInterrupt(&provider, arguments, &result);
            if (status != OS_EOK) {
                return status;
            }
        }
        index++;
    }
    if (index != names) {
        return OS_EINVALPARAMS;
    }
    *interrupt = result;
    return OS_EOK;
}


static void
__PciReserveDomain(
    _In_ const struct FdtResources* nodes,
    _In_ int depth,
    _InOut_ void* context)
{
    struct __PciWalk* walk = context;
    const struct FdtResources* node = &nodes[depth];
    enum FdtPciHostType type;
    uint32_t domain;
    uint32_t* domains;

    if (walk->Status != OS_EOK || depth == 0) {
        return;
    }
    if (node->Disabled || node->AncestorDisabled) {
        return;
    }
    if (!FdtPciHostType(&node->View, &type) ||
        FdtScalar(&node->View, "linux,pci-domain", &domain) != OS_EOK) {
        return;
    }

    // Reserve declared domains even when a host's resource description later
    // proves unusable. Another controller must not inherit its explicit identity.
    domains = realloc(walk->ReservedDomains,
        (walk->ReservedCount + 1) * sizeof(*domains));
    if (domains == NULL) {
        walk->Status = OS_EOOM;
        return;
    }
    walk->ReservedDomains = domains;
    domains[walk->ReservedCount++] = domain;
}

static void
__PciVisit(
    _In_ const struct FdtResources* nodes,
    _In_ int depth,
    _InOut_ void* context)
{
    struct __PciWalk* walk = context;
    const struct FdtResources* node = &nodes[depth];
    enum FdtPciHostType type;

    if (walk->Status != OS_EOK) {
        return;
    }
    if (depth && !node->Disabled && !node->AncestorDisabled &&
        FdtPciHostType(&node->View, &type)) {
        __EmitPciHost(nodes, depth, node, walk);
    }
}

oserr_t
FdtEnumeratePciHosts(
    _In_ const void* blob,
    _In_ size_t length,
    _In_ FdtPciHostFn callback,
    _InOut_ void* context)
{
    struct __PciWalk walk = { .Blob = blob, .Length = length, .Callback = callback, .Context = context };
    oserr_t status;

    if (callback == NULL) {
        return OS_EINVALPARAMS;
    }
    status = FdtWalkResources(blob, length, __PciReserveDomain, &walk);
    if (status == OS_EOK && walk.Status == OS_EOK) {
        status = FdtWalkResources(blob, length, __PciVisit, &walk);
    }
    free(walk.ReservedDomains);
    return status != OS_EOK ? status : walk.Status;
}
