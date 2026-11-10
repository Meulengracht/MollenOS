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
 */

#include <bus/pci/bars.h>
#include <bus/pci/config.h>
#include <bus/pci/device.h>
#include <bus/pci/host-private.h>
#include <ddk/utils.h>

struct __PciAddressRange {
    uint64_t Base;
    uint64_t Limit;
};

// kind is 0 for I/O, 1 for memory, and 2 for prefetchable memory.
static struct __PciAddressRange
__BridgeRange(
    _In_ PciDevice_t* bridge,
    _In_ unsigned int kind)
{
    struct __PciAddressRange range;
    PciHost_t*               host = bridge->Host;
    uint32_t                 value;

    if (kind == 0) {
        value = PciRead16(host, bridge->Bus, bridge->Slot, bridge->Function, 0x1c);
        
        range.Base = (value & 0xf0) << 8;
        range.Limit = (value & 0xf000) | 0xfff;
        
        if ((value & 0x0f0f) == 0x0101) {
            value = PciRead32(host, bridge->Bus, bridge->Slot, bridge->Function, 0x30);
            range.Base |= (uint64_t)(value & 0xffff) << 16;
            range.Limit |= value & 0xffff0000U;
        } else if (value & 0x0f0f) {
            range.Base = 1;
            range.Limit = 0;
        }
    } else {
        value = PciRead32(host, bridge->Bus, bridge->Slot, bridge->Function, kind == 1 ? 0x20 : 0x24);
        
        range.Base = (uint64_t)(value & 0xfff0) << 16;
        range.Limit = (value & 0xfff00000U) | 0xfffff;
        
        if (kind == 2 && (value & 0x000f000f) == 0x00010001) {
            range.Base |= (uint64_t)PciRead32(host, bridge->Bus, bridge->Slot,
                bridge->Function, 0x28) << 32;
            range.Limit |= (uint64_t)PciRead32(host, bridge->Bus, bridge->Slot,
                bridge->Function, 0x2c) << 32;
        } else if (value & 0x000f000f) {
            range.Base = 1;
            range.Limit = 0;
        }
    }
    return range;
}

static int
__OnDevicePath(
    _In_ PciDevice_t* device,
    _In_ PciDevice_t* bridge)
{
    while (device != NULL) {
        if (device == bridge) {
            return 1;
        }
        device = device->Parent;
    }
    return 0;
}

static void
__SkipRange(
    _In_    uint64_t  base,
    _In_    uint64_t  limit,
    _In_    uint64_t  address,
    _In_    uint64_t  size,
    _InOut_ uint64_t* last)
{
    if (base <= limit && address <= limit && base <= address + size - 1 && limit > *last) {
        *last = limit;
    }
}

static void
__FindConflict(
    _In_    PciDevice_t* root,
    _In_    PciDevice_t* target,
    _In_    int          io,
    _In_    uint64_t     address,
    _In_    uint64_t     size,
    _InOut_ uint64_t*    last)
{

    for (unsigned int i = 0; i < 6; i++) {
        const struct PciBar* bar = &root->Resources.Bars[i];
        if (bar->BusAddress == 0 || bar->Size == 0 || (bar->Space == 1) != io) {
            continue;
        }
        
        // Even an unusable assigned BAR must not be overlapped by a new range.
        __SkipRange(
            bar->BusAddress,
            bar->Size - 1 > UINT64_MAX - bar->BusAddress ? UINT64_MAX : bar->BusAddress + bar->Size - 1,
            address,
            size,
            last
        );
    }

    // A sibling bridge decodes its entire window, including unused space.
    if (root->IsBridge && root->Parent != NULL && !__OnDevicePath(target, root)) {
        struct __PciAddressRange range;

        for (unsigned int i = io ? 0 : 1; i <= (io ? 0 : 2); i++) {
            range = __BridgeRange(root, i);
            __SkipRange(range.Base, range.Limit, address, size, last);
        }
    }

    foreach (element, &root->Children) {
        __FindConflict(element->value, target, io, address, size, last);
    }
}

static oserr_t
__FindBarAddress(
    _In_  PciDevice_t*   device,
    _In_  struct PciBar* bar,
    _Out_ uint64_t*      addressOut)
{
    const struct PciHostResources* resources = &device->Host->Resources;
    const struct PciAddressWindow* window;
    struct __PciAddressRange       range;
    struct __PciAddressRange       bridgeRange;
    PciDevice_t*                   parent;
    uint64_t                       address;
    uint64_t                       last;
    unsigned int                   i;
    unsigned int                   kind;
    int                            io = bar->Space == 1;
    int                            prefetch = !io && (bar->Attributes & 8);

    if (bar->Size == 0 || (bar->Size & (bar->Size - 1))) {
        return OS_EINVALPARAMS;
    }

    for (i = 0; i < resources->Count; i++) {
        window = &resources->Windows[i];
        
        if ((window->Space == PciAddressIo) != io || window->Length == 0) {
            continue;
        }

        if (window->Space < PciAddressIo || window->Space > PciAddressMemory64 ||
            window->Length - 1 > UINT64_MAX - window->BusBase) {
            continue;
        }

        // A non-prefetchable BAR cannot use a prefetchable forwarding window.
        if (!prefetch && (window->Attributes & PCI_WINDOW_PREFETCHABLE)) {
            continue;
        }

        kind = io ? 0 : ((window->Attributes & PCI_WINDOW_PREFETCHABLE) ? 2 : 1);
        range.Base = window->BusBase;
        range.Limit = window->BusBase + window->Length - 1;
        
        if (bar->Space != 3 && range.Limit > UINT32_MAX) {
            range.Limit = UINT32_MAX;
        }

        // The obsolete below-1-MiB memory BAR has a narrower address limit.
        if (!io && (bar->Attributes & 6) == 2 && range.Limit > 0xfffff) {
            range.Limit = 0xfffff;
        }

        for (parent = device->Parent; parent != NULL && parent->Parent != NULL; parent = parent->Parent) {
            if (!(PciRead16(parent->Host, parent->Bus, parent->Slot, parent->Function, 0x04) &
                    (io ? PCI_COMMAND_PORTIO : PCI_COMMAND_MMIO))) {
                range.Base = 1;
                range.Limit = 0;
                break;
            }
            bridgeRange = __BridgeRange(parent, kind);
            if (bridgeRange.Base > range.Base) {
                range.Base = bridgeRange.Base;
            }
            if (bridgeRange.Limit < range.Limit) {
                range.Limit = bridgeRange.Limit;
            }
        }

        // Address zero remains the unassigned encoding in the device inventory.
        address = range.Base ? range.Base : bar->Size;
        while (address <= range.Limit && bar->Size - 1 <= UINT64_MAX - address) {
            address = (address + bar->Size - 1) & ~(bar->Size - 1);
            
            if (address > range.Limit || bar->Size - 1 > range.Limit - address) {
                break;
            }

            last = address - 1;
            __FindConflict(device->Host->RootDevice, device, io, address, bar->Size, &last);
            if (last < address) {
                *addressOut = address;
                return OS_EOK;
            }
            if (last == UINT64_MAX) {
                break;
            }

            address = last + 1;
        }
    }
    return OS_EOOM;
}

static oserr_t
__ProgramBar(
    _In_ PciDevice_t* device,
    _In_ unsigned int index,
    _In_ uint64_t address)
{
    PciHost_t*     host = device->Host;
    struct PciBar* bar = &device->Resources.Bars[index];
    struct PciBar  assigned;
    uint32_t       low;
    uint32_t       high = 0;
    uint32_t       value;
    uint32_t       mask = bar->Space == 1 ? ~3U : ~15U;
    uint16_t       command;
    uint16_t       disabled;
    size_t         reg = 0x10 + index * 4;
    int            matched;

    if (bar->Space == 3 && index == 5) {
        return OS_EINVALPARAMS;
    }
    
    PciDescribeBar(host, bar->Space, bar->Attributes, address, bar->Size, &assigned);
    if (assigned.State != PciBarAssigned) {
        return OS_ENOTSUPPORTED;
    }
    
    command = PciRead16(host, device->Bus, device->Slot, device->Function, 0x04);
    disabled = command & ~(PCI_COMMAND_MMIO | PCI_COMMAND_PORTIO | PCI_COMMAND_BUSMASTER);
    PciWrite16(host, device->Bus, device->Slot, device->Function, 0x04, disabled);
    if (PciRead16(host, device->Bus, device->Slot, device->Function, 0x04) != disabled) {
        __PciHostSetState(host, PciHostConfigurationUnknown, OS_ENOTSUPPORTED);
        return OS_EUNKNOWN;
    }
    
    low = PciRead32(host, device->Bus, device->Slot, device->Function, reg);
    if (bar->Space == 3) {
        high = PciRead32(host, device->Bus, device->Slot, device->Function, reg + 4);
        PciWrite32(host, device->Bus, device->Slot, device->Function, reg + 4, address >> 32);
    }

    value = ((uint32_t)address & mask) | bar->Attributes;
    PciWrite32(host, device->Bus, device->Slot, device->Function, reg, value);
    matched = PciRead32(host, device->Bus, device->Slot, device->Function, reg) == value;
    
    if (bar->Space == 3) {
        matched = matched && PciRead32(host, device->Bus, device->Slot, device->Function,
            reg + 4) == (uint32_t)(address >> 32);
    }

    if (!matched) {
        PciWrite32(host, device->Bus, device->Slot, device->Function, reg, low);
        if (bar->Space == 3) {
            PciWrite32(host, device->Bus, device->Slot, device->Function, reg + 4, high);
        }
        // Restore decoding only if both original BAR halves were restored.
        if (PciRead32(host, device->Bus, device->Slot, device->Function, reg) == low &&
            (bar->Space != 3 || PciRead32(host, device->Bus, device->Slot, device->Function, reg + 4) == high)) {
            PciWrite16(host, device->Bus, device->Slot, device->Function, 0x04, command);
            if (PciRead16(host, device->Bus, device->Slot, device->Function, 0x04) != command) {
                __PciHostSetState(host, PciHostConfigurationUnknown, OS_ENOTSUPPORTED);
            }
        } else {
            __PciHostSetState(host, PciHostConfigurationUnknown, OS_ENOTSUPPORTED);
        }
        return OS_EUNKNOWN;
    }

    // Keep the verified address reserved even if restoring the command fails.
    *bar = assigned;
   
    PciWrite16(host, device->Bus, device->Slot, device->Function, 0x04, command);
    if (PciRead16(host, device->Bus, device->Slot, device->Function, 0x04) != command) {
        PciWrite16(host, device->Bus, device->Slot, device->Function, 0x04, disabled);
        __PciHostSetState(host, PciHostConfigurationUnknown, OS_ENOTSUPPORTED);
        return OS_EUNKNOWN;
    }

    WARNING("PCI %u:%u:%u.%u BAR%u assigned: bus=0x%llx cpu=0x%llx size=0x%llx",
        host->Identification.Segment, device->Bus, device->Slot, device->Function, index,
        (unsigned long long)bar->BusAddress, (unsigned long long)bar->CpuAddress,
        (unsigned long long)bar->Size);
    return OS_EOK;
}

static void
__ReportPlacementTree(
    _In_ PciDevice_t* device,
    _In_ PciDevice_t* target)
{
    for (unsigned int i = 0; i < 6; i++) {
        const struct PciBar* bar = &device->Resources.Bars[i];
        if (bar->BusAddress == 0 || bar->Size == 0) {
            continue;
        }

        ERROR("PCI placement occupied %u:%u.%u BAR%u: space=%u state=%u bus=0x%llx size=0x%llx",
            device->Bus, device->Slot, device->Function, i, bar->Space, bar->State,
            (unsigned long long)bar->BusAddress, (unsigned long long)bar->Size);
    }

    if (device->IsBridge && device->Parent != NULL) {
        uint16_t command = PciRead16(device->Host, device->Bus, device->Slot, device->Function, 0x04);
        uint32_t memory = PciRead32(device->Host, device->Bus, device->Slot, device->Function, 0x20);
        uint32_t prefetch = PciRead32(device->Host, device->Bus, device->Slot, device->Function, 0x24);
        ERROR("PCI placement bridge %u:%u.%u: on-path=%u command=0x%04x memory=0x%08x prefetch=0x%08x",
            device->Bus, device->Slot, device->Function, __OnDevicePath(target, device),
            command, memory, prefetch);
        
        for (unsigned int i = 0; i < 3; i++) {
            struct __PciAddressRange range = __BridgeRange(device, i);
            ERROR("PCI placement bridge window: kind=%u bus=0x%llx-0x%llx",
                i, (unsigned long long)range.Base, (unsigned long long)range.Limit);
        }
    }
    
    foreach (element, &device->Children) {
        __ReportPlacementTree(element->value, target);
    }
}

static void
__ReportBarPlacement(
    _In_ PciDevice_t* device,
    _In_ unsigned int index)
{
    const struct PciHostResources* resources = &device->Host->Resources;
    const struct PciBar*           bar = &device->Resources.Bars[index];
    const struct PciAddressWindow* window;

    // Report the inputs only after placement fails. No configuration is changed.
    ERROR("PCI placement %u:%u:%u.%u BAR%u: space=%u attributes=0x%x size=0x%llx host-windows=%zu",
        device->Host->Identification.Segment, device->Bus, device->Slot, device->Function,
        index, bar->Space, bar->Attributes, (unsigned long long)bar->Size, resources->Count);
    for (unsigned int i = 0; i < resources->Count; i++) {
        window = &resources->Windows[i];
        ERROR("PCI placement host window %u: space=%u attributes=0x%x bus=0x%llx cpu=0x%llx size=0x%llx",
            i, window->Space, window->Attributes, (unsigned long long)window->BusBase,
            (unsigned long long)window->PhysicalBase, (unsigned long long)window->Length);
    }
    __ReportPlacementTree(device->Host->RootDevice, device);
}

static oserr_t
__AssignDeviceBars(
    _In_ PciDevice_t* device)
{
    oserr_t status;
    int     changed = 0;

    for (unsigned int i = 0; i < 6; i++) {
        uint64_t address;
        
        if (device->Resources.Bars[i].State != PciBarUnassigned) {
            continue;
        }
        
        status = __FindBarAddress(device, &device->Resources.Bars[i], &address);
        if (status == OS_EOK) {
            status = __ProgramBar(device, i, address);
        } else {
            __ReportBarPlacement(device, i);
        }

        if (status != OS_EOK) {
            ERROR("PCI %u:%u:%u.%u BAR%u assignment failed (%u)",
                device->Host->Identification.Segment, device->Bus, device->Slot, device->Function, i, status);
            return status;
        }
        changed = 1;
    }

    if (changed && device->Handler != NULL && device->Attachment == NULL) {
        // An early attachment may have failed because its BAR was unassigned.
        device->AttachmentStatus = OS_EBUSY;
    }

    foreach (element, &device->Children) {
        status = __AssignDeviceBars(element->value);
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}

oserr_t
PciAssignHostBars(
    _In_ PciDevice_t* root)
{
    const struct PciHostResources* resources = &root->Host->Resources;

    // ACPI windows validate firmware assignments but never authorize moving
    // or assigning BARs. Device-tree hosts explicitly enable that policy.
    if (!resources->AssignBars) {
        return OS_EOK;
    }

    // Configuration space may not match the inventory after a failed rollback.
    if (root->Host->Status.State == PciHostConfigurationUnknown) {
        return root->Host->Status.Error;
    }
    return __AssignDeviceBars(root);
}
