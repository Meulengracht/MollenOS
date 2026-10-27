/**
 * MollenOS
 *
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
 *
 * MollenOS PCI Bus Driver 
 * - Enumerates the bus and registers the devices/controllers
 *   available in the system
 */

//#define __TRACE

#include <ddk/utils.h>
#include <bus/pci/bars.h>
#include <bus/pci/host-private.h>
#include <bus/pci/config.h>
#include <ddk/devices/bus.h>
#include <string.h>

uint64_t
PciValidateBarSize(
    _In_ uint64_t base,
    _In_ uint64_t maxBase,
    _In_ uint64_t mask)
{
    uint64_t encodedSize = mask & maxBase;
    uint64_t size;

    if (!encodedSize) {
        return 0;
    }

    // BAR probing returns an address mask. Isolate its least-significant set
    // bit to obtain the actual power-of-two byte length. I/O resource lengths
    // are counts, so returning size - 1 truncates the final byte and rejects a
    // capability whose range ends exactly at the BAR boundary.
    size = encodedSize & ~(encodedSize - 1);
    if (base == maxBase && ((base | (size - 1)) & mask) != mask) {
        return 0;
    }
    return size;
}

void
PciDescribeBar(
    _In_  PciHost_t*     host,
    _In_  uint32_t       space,
    _In_  uint32_t       attributes,
    _In_  uint64_t       address,
    _In_  uint64_t       size,
    _Out_ struct PciBar* bar)
{
    oserr_t oserr;

    // A zero-sized BAR stays in the initial, absent state.
    memset(bar, 0, sizeof(*bar));
    bar->BusAddress = address;
    bar->Size = size;
    bar->Space = space;
    bar->Attributes = attributes;
    
    if (!size) {
        return;
    }
    
    if (!address) {
        bar->State = PciBarUnassigned;
        return;
    }
    
    // The last byte of the bus range must fit in a 64-bit address.
    if (size - 1 > UINT64_MAX - address) {
        bar->State = PciBarInvalid;
        return;
    }
    
    bar->CpuAddress = address;
    if (host->Operations->Translate != NULL) {
        oserr = host->Operations->Translate(
            host,
            space,
            address,
            size,
            &bar->CpuAddress
        );
        if (oserr != OS_EOK) {
            TRACE("PciDescribeBar translation failed for space %u at 0x%llx+0x%llx: %u",
                space, (unsigned long long)address, (unsigned long long)size, oserr);
            bar->CpuAddress = 0;
            bar->State = PciBarOutsideWindow;
            return;
        }
    }
    
    // Translation must also leave the last byte within the CPU address range.
    if (size - 1 > UINT64_MAX - bar->CpuAddress) {
        bar->State = PciBarInvalid;
        return;
    }
    bar->State = PciBarAssigned;
}

void
PciProbeBars(
    _In_  PciHost_t*         bus,
    _In_  const BusDevice_t* device,
    _In_  uint32_t           headerType,
    _Out_ struct PciBar*     bars)
{
    // Bridge headers have two BARs. Device headers have six.
    int      count = (headerType & 0x1) == 0x1 ? 2 : 6;
    uint16_t command;

    TRACE("PciProbeBars(%u:%u:%u.%u, header=0x%x)", bus->Identification.Segment,
        device->Bus, device->Slot, device->Function, headerType);

    memset(bars, 0, sizeof(struct PciBar) * 6);

    // Size probing temporarily writes address bits. Disable decoding so those
    // transient addresses cannot redirect a real access to another resource.
    command = PciRead16(bus, device->Bus, device->Slot, device->Function, 0x04);
    PciWrite16(bus, device->Bus, device->Slot, device->Function, 0x04,
        command & ~(PCI_COMMAND_MMIO | PCI_COMMAND_PORTIO));

    // Probe each BAR while decoding is disabled.
    for (int i = 0; i < count; i++) {
        uint32_t space32, size32, mask32;
        uint64_t space64, size64, mask64;
        int      barIndex = i;
        uint32_t memorySpace = 0;
        uint32_t attributes;
        size_t   offset = 0x10 + (i << 2);

        // Calculate the initial mask 
        mask32 = (headerType & 0x1) == 0x1 ? ~0x7FF : 0xFFFFFFFF;

        // Save the address, read the size mask, then restore the address.
        space32 = PciRead32(bus, device->Bus, device->Slot, device->Function, offset);
        PciWrite32(bus, device->Bus, device->Slot, device->Function, offset, space32 | mask32);
        size32 = PciRead32(bus, device->Bus, device->Slot, device->Function, offset);
        PciWrite32(bus, device->Bus, device->Slot, device->Function, offset, space32);

        // Clear all-ones values before decoding the address and size mask.
        if (size32 == 0xFFFFFFFF) {
            size32 = 0;
        }
        if (space32 == 0xFFFFFFFF) {
            space32 = 0;
        }

        attributes = space32 & ((space32 & 1) ? 3 : 15);

        // Bit 0 selects port I/O space.
        if (space32 & 0x1) {
            mask64  = 0xFFFFFFFC;
            size64  = size32;
            space64 = space32 & 0xFFFFFFFC;

            size64 = PciValidateBarSize(space64, size64, mask64);
        }
        // Bit 2 selects a 64-bit memory BAR, which uses two registers.
        else if (space32 & 0x4) {
            if (i + 1 >= count) {
                bars[i].State = PciBarInvalid;
                bars[i].Attributes = attributes;
                bars[i].Space = 3;
                break;
            }
            memorySpace = 3;
            space64 = space32 & 0xFFFFFFF0;
            size64  = size32 & 0xFFFFFFF0;
            mask64  = 0xFFFFFFFFFFFFFFF0;
            
            // The next BAR register stores the upper address bits.
            i++;
            offset = 0x10 + (i << 2);

            // Keep both halves in probe mode before reading either mask.
            PciWrite32(bus, device->Bus, device->Slot, device->Function,
                offset - 4, 0xFFFFFFFFU);

            // Save the upper address bits before probing the complete BAR.
            space32 = PciRead32(bus, device->Bus, device->Slot, device->Function, offset);
            PciWrite32(bus, device->Bus, device->Slot, device->Function, offset, 0xFFFFFFFF);
            size32 = PciRead32(bus, device->Bus, device->Slot, device->Function, offset);
            size64 = PciRead32(bus, device->Bus, device->Slot, device->Function, offset - 4) & 0xFFFFFFF0;
            PciWrite32(bus, device->Bus, device->Slot, device->Function, offset, space32);

            PciWrite32(bus, device->Bus, device->Slot, device->Function,
                offset - 4, (uint32_t)space64 | attributes);

            // Combine both halves before converting the mask to a byte count.
            space64 |= ((uint64_t)space32 << 32);
            size64  |= ((uint64_t)size32 << 32);
            size64 = PciValidateBarSize(space64, size64, mask64);
        } else {
            memorySpace = 2;
            space64 = space32 & 0xFFFFFFF0;
            size64  = size32 & 0xFFFFFFF0;
            mask64  = 0xFFFFFFF0;

            size64 = PciValidateBarSize(space64, size64, mask64);
        }
        
        PciDescribeBar(
            bus,
            memorySpace ? memorySpace : 1,
            attributes,
            space64,
            size64,
            &bars[barIndex]
        );
        TRACE("PciProbeBars BAR%u space=%u bus=0x%llx cpu=0x%llx size=0x%llx state=%u",
            (unsigned)barIndex, bars[barIndex].Space, (unsigned long long)space64,
            (unsigned long long)bars[barIndex].CpuAddress, (unsigned long long)size64,
            bars[barIndex].State);
    }

    PciWrite16(bus, device->Bus, device->Slot, device->Function, 0x04, command);
}
