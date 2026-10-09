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

#include <firmware/pci.h>
#include <firmware/resources.h>
#include <firmware/bcm.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Holds the PCI properties needed after a Device Tree node is recognized as
 * a supported host controller. The property bytes remain in the original tree.
 */
struct __PciNode {
    int            HasDomain;
    uint32_t       Domain;
    const uint8_t* InterruptMap;
    uint32_t       InterruptMapLength;
    const uint8_t* InterruptMask;
    uint32_t       InterruptMaskLength;
};

/**
 * @brief Keeps state while the Device Tree is checked for PCI host controllers.
 * A domain number identifies a group of PCI buses. Firmware-provided numbers
 * are collected first so automatically chosen numbers cannot take their place.
 */
struct __PciWalk {
    const void*  Blob;
    size_t       Length;
    uint32_t*    ReservedDomains;
    size_t       ReservedCount;
    uint64_t     NextDomain;
    oserr_t      Status;
    FdtPciHostFn Callback;
    void*        Context;
};

// Choose a domain number for a host whose firmware did not provide one. The
// full list is checked so this choice cannot take a number used by a later host.
static oserr_t
__PciNextDomain(
    _InOut_ struct __PciWalk* walk,
    _Out_   uint32_t*         domainOut)
{
    size_t index;

    // Firmware numbers are not required to be in order. After skipping a used
    // number, restart because the next candidate may also appear later in the list.
    // Numbers assigned by this function are already below NextDomain.
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

// Use the Device Tree's hardware names to select only controllers this code
// knows how to configure; an unknown host must not be treated as generic ECAM.
int
FdtPciHostType(
    _In_  const struct FdtNode* node,
    _Out_ enum FdtPciHostType*  type)
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

// Read either host-to-device ranges or device-to-memory ranges. Their firmware
// entries have the same layout; only the source property, destination list, and
// direction of address translation differ.
static int
__PciReadWindows(
    _In_    const struct FdtResources* nodes,
    _In_    int                        depth,
    _In_    const struct FdtResources* node,
    _In_    int                        dma,
    _Out_   struct FdtPciWindow*       windows,
    _InOut_ uint32_t*                  windowCount)
{
    const uint8_t*       ranges;
    uint32_t             rangesLength;
    uint32_t             addressCells = nodes[depth - 1].AddressCells;
    uint32_t             cells;
    uint32_t             offset;
    struct FdtPciWindow* window;

    ranges = dma ? node->DmaRanges : node->Ranges;
    rangesLength = dma ? node->DmaRangesLength : node->RangesLength;
    if (rangesLength == 0) {
        return 1;
    }
    if (node->AddressCells != 3 || node->SizeCells == 0 || node->SizeCells > 2) {
        return 0;
    }

    cells = 3 + addressCells + node->SizeCells;
    if (rangesLength % (cells * 4) != 0 || rangesLength / (cells * 4) > 16) {
        return 0;
    }

    for (offset = 0; offset < rangesLength; offset += cells * 4) {
        window = &windows[(*windowCount)++];
        window->Attributes = FdtReadBe32(ranges + offset);
        window->Space = FDT_PCI_SPACE_FROM_ATTRIBUTES(window->Attributes);
        window->BusBase = FdtReadCells(ranges + offset + 4, 2);
        window->PhysicalBase = FdtReadCells(ranges + offset + 12, addressCells);
        window->Length = FdtReadCells(
            ranges + offset + (3 + addressCells) * 4,
            node->SizeCells
        );
        
        if (window->Length == 0 || window->Length - 1 > UINT64_MAX - window->BusBase ||
            !FdtTranslateAddress(nodes, depth - 1, window->Length, &window->PhysicalBase, dma)) {
            return 0;
        }
    }
    return 1;
}

// Read the firmware bus-number limits and cap them to the configuration space
// this controller can actually expose. This keeps device scanning within the
// host's declared bus range and, for ECAM, within its mapped memory range.
static int
__PciReadBusRange(
    _In_  const struct FdtNode*    node,
    _In_  const struct FdtPciHost* host,
    _Out_ uint8_t*                 busStartOut,
    _Out_ uint8_t*                 busEndOut)
{
    const uint8_t* busRange;
    uint32_t       busRangeLength;
    uint32_t       busStart = 0;
    uint32_t       busEnd = 255;
    uint64_t       busesDecoded;

    busRange = FdtProperty(node, "bus-range", &busRangeLength);
    if (busRange != NULL && busRangeLength != 8) {
        return 0;
    }
    if (busRangeLength == 8) {
        busStart = FdtReadBe32(busRange);
        busEnd = FdtReadBe32(busRange + 4);
    }
    if (busStart > 255 || busEnd > 255 || busEnd < busStart) {
        WARNING("FdtEnumeratePciHosts invalid bus-range %u-%u", busStart, busEnd);
        return 0;
    }

    busesDecoded = host->Type == FdtPciHostEcam ? host->EcamLength >> 20 : 256;
    if (busesDecoded == 0) {
        return 0;
    }
    if ((uint64_t)(busEnd - busStart) + 1 > busesDecoded) {
        busEnd = busStart + (uint32_t)busesDecoded - 1;
    }

    *busStartOut = (uint8_t)busStart;
    *busEndOut = (uint8_t)busEnd;
    return 1;
}

// Build a complete, checked host description before invoking the caller. If
// any required range cannot be decoded, skip this host rather than expose a
// partly initialized description that later code could mistake for usable.
static void
__EmitPciHost(
    _In_    const struct FdtResources* nodes,
    _In_    int                        depth,
    _In_    const struct FdtResources* node,
    _InOut_ struct __PciWalk*          walk)
{
    struct __PciNode  pci = { 0 };
    struct FdtPciHost host = { 0 };
    oserr_t           status;

    // The register range identifies the host's control registers. Without a
    // trustworthy range, consumers cannot safely access or configure the host.
    if (node->Malformed || node->AncestorMalformed || node->RegisterStatus != OS_EOK) {
        WARNING("FdtEnumeratePciHosts unsupported reg layout for ECAM host");
        return;
    }

    host.NodeOffset = node->NodeOffset;
    host.EcamBase   = node->PhysicalBase;
    host.EcamLength = node->PhysicalLength;
    FdtPciHostType(&node->View, &host.Type);
    
    // Broadcom controllers need platform-specific settings in addition to the
    // common PCI properties, so let their policy fill and validate those fields.
    if (FdtBcmHostPolicy(&node->View, &host) != OS_EOK) {
        return;
    }
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
    
    // "ranges" maps processor accesses to PCI resources.
    if (!__PciReadWindows(nodes, depth, node, 0, host.Windows, &host.WindowCount)) {
        return;
    }

    // "dma-ranges" maps device memory requests to system addresses.
    if (!__PciReadWindows(nodes, depth, node, 1, host.DmaWindows, &host.DmaWindowCount)) {
        return;
    }

    if (!__PciReadBusRange(&node->View, &host, &host.BusStart, &host.BusEnd)) {
        return;
    }

    // Preserve firmware's identity when supplied. Otherwise assign a number
    // that was not reserved during the first tree walk.
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

// Compare enabled memory nodes with uncertain DMA ranges. Firmware can describe
// a range larger than installed RAM, so its size or starting address alone is
// not enough to decide whether a device may safely use it for memory buffers.
static void
__ClassifyMemory(
    _In_    const struct FdtResources* nodes,
    _In_    int                        depth,
    _InOut_ void*                      context)
{
    struct FdtPciHost*         host = context;
    const struct FdtResources* node = &nodes[depth];
    uint32_t                   ac;
    uint32_t                   sc;
    uint32_t                   stride;
    uint64_t                   base;
    uint64_t                   length;
    struct FdtPciWindow*       window;

    if (node->Disabled || node->AncestorDisabled || !node->IsMemory || node->Malformed || node->AncestorMalformed || depth == 0) {
        return;
    }

    // A memory node's "reg" entries use the parent bus's address and size cell
    // counts. Validate that layout before reading the entries as byte ranges.
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
                (window->Space == FdtPciSpaceMemory32 || window->Space == FdtPciSpaceMemory64) &&
                (FdtContainsRange(window->PhysicalBase, window->Length, base, length) ||
                 FdtContainsRange(base, length, window->PhysicalBase, window->Length))) {
                window->Kind = FdtDmaWindowRam;
            }
        }
    }
}

// Mark known MSI and peer-device destinations first, then compare remaining
// ranges with actual memory nodes. This order avoids labeling an interrupt or
// another device's address space as ordinary RAM.
static void
__ClassifyDmaWindows(
    _InOut_ struct FdtPciHost* host)
{
    struct FdtPciMsi     msi;
    struct FdtPciWindow* window;
    int                  hasMip;

    hasMip = FdtResolvePciMsi(host, &msi) == OS_EOK && msi.IsMip;

    for (uint32_t i = 0; i < host->DmaWindowCount; i++) {
        window = &host->DmaWindows[i];
        
        // MIP interrupts are raised by writes to their doorbell address. Check
        // both bus and translated physical addresses to avoid matching an
        // unrelated range that happens to contain the same bus address.
        if (hasMip && FdtContainsRange(window->BusBase, window->Length,
                msi.DoorbellBase, msi.DoorbellLength) &&
            msi.RegisterBase >= window->PhysicalBase &&
            msi.RegisterBase - window->PhysicalBase == msi.DoorbellBase - window->BusBase) {
            window->Kind = FdtDmaWindowMsi;
            continue;
        }
        
        // A DMA destination inside a processor-to-PCI window points at another
        // PCI resource, rather than at system RAM.
        for (uint32_t j = 0; j < host->WindowCount; j++) {
            if (FdtContainsRange(host->Windows[j].PhysicalBase, host->Windows[j].Length,
                    window->PhysicalBase, window->Length)) {
                window->Kind = FdtDmaWindowPeer;
                break;
            }
        }
    }
    
    // A device's DMA range may include addresses that are not backed by RAM.
    // Compare it with the firmware's descriptions of installed memory instead
    // of guessing from the range's order or from an address that starts at zero.
    FdtWalkResources(host->Blob, host->BlobLength, __ClassifyMemory, host);
}

// Convert only when one firmware window contains the entire request. Returning
// the first address for a partly covered range would make later accesses cross
// into an unmapped or differently translated region.
oserr_t
FdtTranslatePciAddress(
    _In_  const struct FdtPciHost* host,
    _In_  enum FdtPciSpace         space,
    _In_  uint64_t                 address,
    _In_  uint64_t                 length,
    _Out_ uint64_t*                physicalOut)
{
    const struct FdtPciWindow* window;
    uint32_t                   index;
    uint64_t                   displacement;

    if (host == NULL || physicalOut == NULL || host->WindowCount > 16 ||
        length == 0 || length - 1 > UINT64_MAX - address) {
        return OS_EINVALPARAMS;
    }

    for (index = 0; index < host->WindowCount; index++) {
        window = &host->Windows[index];
        // PCI memory addresses of the 32-bit and 64-bit kinds share compatible
        // windows; I/O addresses must still match their own kind exactly.
           if ((window->Space != space &&
               !((space == FdtPciSpaceMemory32 || space == FdtPciSpaceMemory64) &&
                (window->Space == FdtPciSpaceMemory32 || window->Space == FdtPciSpaceMemory64)))) {
            continue;
        }

        if (!FdtContainsRange(window->BusBase, window->Length, address, length)) {
            continue;
        }

        displacement = address - window->BusBase;
        if (displacement > UINT64_MAX - window->PhysicalBase ||
            length - 1 > UINT64_MAX - window->PhysicalBase - displacement) {
            return OS_EINVALPARAMS;
        }

        *physicalOut = window->PhysicalBase + displacement;
        return OS_EOK;
    }
    return OS_ENOENT;
}

// Parse one interrupt-map entry. The controller named by the entry determines
// how many address and interrupt cells follow it, so it also determines where
// the next entry starts. A well-formed nonmatching entry is not an error.
static oserr_t
__PciDecodeInterruptMapEntry(
    _In_  const struct FdtPciHost* host,
    _In_  const uint8_t*           entry,
    _In_  uint32_t                 available,
    _In_  const uint32_t*          key,
    _Out_ uint32_t*                strideOut,
    _Out_ int*                     matchesOut,
    _Out_ struct FdtInterrupt*     interruptOut)
{
    struct FdtResources controller = { 0 };
    uint32_t            stride;
    uint32_t            index;
    uint32_t            mask;
    const uint8_t*      specifier;
    int                 matches = 1;
    oserr_t             status;

    memset(interruptOut, 0, sizeof(*interruptOut));
    if (available < 20) {
        return OS_EINVALPARAMS;
    }

    status = FdtFindResources(
        host->Blob,
        host->BlobLength,
        FdtReadBe32(entry + 16),
        &controller
    );
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
    if (stride > available) {
        return OS_EINVALPARAMS;
    }

    for (index = 0; index < 4; index++) {
        mask = host->InterruptMask == NULL ? UINT32_MAX :
                FdtReadBe32(host->InterruptMask + index * 4);
        if ((key[index] & mask) != (FdtReadBe32(entry + index * 4) & mask)) {
            matches = 0;
        }
    }
    if (matches) {
        specifier = entry + (5 + controller.AddressCells) * 4;
        status = FdtGicInterrupt(&controller, specifier, interruptOut);
        if (status != OS_EOK) {
            return status;
        }
    }

    *strideOut = stride;
    *matchesOut = matches;
    return OS_EOK;
}

// Resolve the older pin-based PCI interrupt path. Each interrupt-map entry can
// have a different controller format, so its referenced controller determines
// how many cells must be read before the next entry begins.
oserr_t
FdtResolvePciInterrupt(
    _In_  const struct FdtPciHost* host,
    _In_  unsigned int             bus,
    _In_  unsigned int             slot,
    _In_  unsigned int             function,
    _In_  unsigned int             pin,
    _Out_ int*                     lineOut,
    _Out_ unsigned int*            flagsOut)
{
    struct FdtInterrupt interrupt;
    uint32_t            key[4];
    uint32_t            offset = 0;
    uint32_t            stride;
    int                 matches;
    oserr_t             status;

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

    // PCI identifies a device by bus, slot, and function. The fourth key value
    // is its interrupt pin; the firmware mask decides which bits are significant.
    key[0] = (bus << 16) | (slot << 11) | (function << 8);
    key[1] = 0;
    key[2] = 0;
    key[3] = pin;

    while (offset < host->InterruptMapLength) {
        status = __PciDecodeInterruptMapEntry(
            host,
            host->InterruptMap + offset,
            host->InterruptMapLength - offset,
            key,
            &stride,
            &matches,
            &interrupt
        );
        if (status != OS_EOK) {
            return status;
        }

        if (matches) {
            *lineOut = interrupt.Line;
            *flagsOut = interrupt.Flags;
            return OS_EOK;
        }
        offset += stride;
    }
    return OS_ENOENT;
}

// Resolve an interrupt by its parallel name list. Extended entries identify
// their own controller; ordinary entries share the host's interrupt parent.
oserr_t
FdtResolvePciNamedInterrupt(
    _In_  const struct FdtPciHost* host,
    _In_  const char*              name,
    _Out_ struct FdtInterrupt*     interrupt)
{
    struct FdtResources provider;
    struct FdtInterrupt result = { 0 };
    const uint8_t*      cells;
    uint32_t            length;
    uint32_t            selected;
    uint32_t            names;
    uint32_t            index = 0;
    uint32_t            offset = 0;
    uint32_t            phandle;
    const uint8_t*      arguments;
    oserr_t             status;

    if (host == NULL || name == NULL || interrupt == NULL) {
        return OS_EINVALPARAMS;
    }

    // Find both the requested position and the number of names so the parsed
    // interrupt list can later be checked for a matching number of entries.
    status = FdtNameIndex(host->InterruptNames, host->InterruptNamesLength, name, &selected, &names);
    if (status != OS_EOK) {
        return status;
    }

    // The extended form carries a controller reference with each interrupt;
    // the regular form uses one shared parent from the host node.
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

        status = FdtNextReference(
            host->Blob,
            host->BlobLength,
            cells,
            length,
            "#interrupt-cells",
            phandle,
            &offset,
            &provider,
            &arguments
        );
        if (status != OS_EOK) {
            return status;
        }
        if (!provider.InterruptCells) {
            return OS_EINVALPARAMS;
        }

        // Decode only the requested entry, but continue parsing all entries so
        // malformed data or a mismatch with the name list is still detected.
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

// First pass: reserve every explicit firmware number, including numbers on
// hosts that later cannot be emitted. This prevents an automatic choice from
// changing the identity firmware assigned to another group of buses.
static void
__PciReserveDomain(
    _In_    const struct FdtResources* nodes,
    _In_    int                        depth,
    _InOut_ void*                      context)
{
    struct __PciWalk*          walk = context;
    const struct FdtResources* node = &nodes[depth];
    enum FdtPciHostType        type;
    uint32_t                   domain;
    uint32_t*                  domains;

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

    // Keep this number unavailable even if the host later proves unusable.
    // Reusing it would make the number identify a different group of buses.
    domains = realloc(walk->ReservedDomains, (walk->ReservedCount + 1) * sizeof(uint32_t));
    if (domains == NULL) {
        walk->Status = OS_EOOM;
        return;
    }

    walk->ReservedDomains = domains;
    domains[walk->ReservedCount++] = domain;
}

// Second pass: emit only enabled nodes recognized as supported PCI hosts. This
// runs after domain reservation so automatically assigned numbers are stable.
static void
__PciVisit(
    _In_    const struct FdtResources* nodes,
    _In_    int                        depth,
    _InOut_ void*                      context)
{
    struct __PciWalk*          walk = context;
    const struct FdtResources* node = &nodes[depth];
    enum FdtPciHostType        type;

    if (walk->Status != OS_EOK) {
        return;
    }

    if (depth && !node->Disabled && !node->AncestorDisabled &&
        FdtPciHostType(&node->View, &type)) {
        __EmitPciHost(nodes, depth, node, walk);
    }
}

// Walk the tree twice: reserve all firmware-selected domain numbers first,
// then build and report usable hosts. The first pass prevents automatic numbers
// from depending on the order in which hosts appear in the tree.
oserr_t
FdtEnumeratePciHosts(
    _In_    const void*  blob,
    _In_    size_t       length,
    _In_    FdtPciHostFn callback,
    _InOut_ void*        context)
{
    oserr_t          status;
    struct __PciWalk walk = {
        .Blob = blob,
        .Length = length,
        .Callback = callback,
        .Context = context
    };

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
