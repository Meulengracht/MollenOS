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

#include "bus.h"
#include <ddk/busdevice.h>
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
    PciHost_t*     host,
    uint32_t       space,
    uint32_t       attributes,
    uint64_t       address,
    uint64_t       size,
    struct PciBar* bar)
{
    oserr_t oserr;

    // Always clear the structure and set initial values
    memset(bar, 0, sizeof(*bar));
    bar->BusAddress = address;
    bar->Size = size;
    bar->Space = space;
    bar->Attributes = attributes;
    
    // Next thing we do is classify the bar
    if (!size) {
        return;
    }
    
    if (!address) {
        bar->State = PciBarUnassigned;
        return;
    }
    
    // Validate that we have a valid BAR address
    if (size - 1 > UINT64_MAX - address) {
        bar->State = PciBarInvalid;
        return;
    }
    
    bar->CpuAddress = address;
    if (host->Operations->Translate != NULL) {
        // Translate the bus address to a CPU address
        oserr = host->Operations->Translate(host, space, address, size, &bar->CpuAddress);
        if (oserr != OS_EOK) {
            bar->CpuAddress = 0;
            bar->State = PciBarOutsideWindow;
            return;
        }
    }
    
    // Validate that we have a valid CPU address range for the BAR
    if (size - 1 > UINT64_MAX - bar->CpuAddress) {
        bar->State = PciBarInvalid;
        return;
    }
    bar->State = PciBarAssigned;
}

void
PciProbeBars(
    _In_ PciHost_t*         bus,
    _In_ const BusDevice_t* device,
    _In_ uint32_t           headerType,
    _Out_ struct PciBar*    bars)
{
    // Buses have 2 io spaces, devices have 6
    int count = (headerType & 0x1) == 0x1 ? 2 : 6;
    int i;
    uint16_t command;

    memset(bars, 0, sizeof(struct PciBar) * 6);

    // Size probing temporarily writes address bits. Disable decoding so those
    // transient addresses cannot redirect a real access to another resource.
    command = PciRead16(bus, device->Bus, device->Slot, device->Function, 0x04);
    PciWrite16(bus, device->Bus, device->Slot, device->Function, 0x04,
        command & ~(PCI_COMMAND_MMIO | PCI_COMMAND_PORTIO));

    /* Iterate all the avilable bars */
    for (i = 0; i < count; i++) {
        uint32_t space32, size32, mask32;
        uint64_t space64, size64, mask64;
        int      barIndex = i;
        uint32_t memorySpace = 0;
        uint32_t attributes;
        size_t   offset = 0x10 + (i << 2);

        // Calculate the initial mask 
        mask32 = (headerType & 0x1) == 0x1 ? ~0x7FF : 0xFFFFFFFF;

        // Read both space and size
        space32 = PciRead32(bus, device->Bus, device->Slot, device->Function, offset);
        PciWrite32(bus, device->Bus, device->Slot, device->Function, offset, space32 | mask32);
        size32 = PciRead32(bus, device->Bus, device->Slot, device->Function, offset);
        PciWrite32(bus, device->Bus, device->Slot, device->Function, offset, space32);

        // Sanitize bounds of values
        if (size32 == 0xFFFFFFFF) {
            size32 = 0;
        }
        if (space32 == 0xFFFFFFFF) {
            space32 = 0;
        }

        attributes = space32 & ((space32 & 1) ? 3 : 15);

        // Which kind of io-space is it, if bit 0 is set, it's io and not mmio 
        if (space32 & 0x1) {
            // Update mask to reflect IO space
            mask64  = 0xFFFC;
            size64  = size32;
            space64 = space32 & 0xFFFC;

            // Correctly update the size of the io
            size64 = PciValidateBarSize(space64, size64, mask64);
        }
        // Ok, its memory, but is it 64 bit or 32 bit? 
        // Bit 2 is set for 64 bit memory space
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
            
            // Calculate a new 64 bit offset
            i++;
            offset = 0x10 + (i << 2);

            // Keep both halves in probe mode before reading either mask.
            PciWrite32(bus, device->Bus, device->Slot, device->Function,
                offset - 4, (uint32_t)space64 | 0xFFFFFFFFU);

            // Read both space and size for 64 bit
            space32 = PciRead32(bus, device->Bus, device->Slot, device->Function, offset);
            PciWrite32(bus, device->Bus, device->Slot, device->Function, offset, 0xFFFFFFFF);
            size32 = PciRead32(bus, device->Bus, device->Slot, device->Function, offset);
            size64 = PciRead32(bus, device->Bus, device->Slot, device->Function, offset - 4) & 0xFFFFFFF0;
            PciWrite32(bus, device->Bus, device->Slot, device->Function, offset, space32);

            PciWrite32(bus, device->Bus, device->Slot, device->Function,
                offset - 4, (uint32_t)space64 | attributes);

            // Set the upper 32 bit of the space
            space64 |= ((uint64_t)space32 << 32);
            size64  |= ((uint64_t)size32 << 32);
            // Correct the size and validate
            size64 = PciValidateBarSize(space64, size64, mask64);
        }
        else {
            memorySpace = 2;
            space64 = space32 & 0xFFFFFFF0;
            size64  = size32 & 0xFFFFFFF0;
            mask64  = 0xFFFFFFF0;

            // Correct the size and validate
            size64 = PciValidateBarSize(space64, size64, mask64);
        }
        PciDescribeBar(bus, memorySpace ? memorySpace : 1, attributes,
            space64, size64, &bars[barIndex]);
    }
    PciWrite16(bus, device->Bus, device->Slot, device->Function, 0x04, command);
}
