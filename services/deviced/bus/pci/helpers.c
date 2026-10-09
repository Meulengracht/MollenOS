/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS X86 Bus Driver 
 * - Enumerates the bus and registers the devices/controllers
 *   available in the system
 */

#include <bus/pci/config.h>

uint16_t PciReadVendorId(
    PciHost_t*   host,
    unsigned int bus,
    unsigned int device,
    unsigned int function)
{
	return PciRead16(host, bus, device, function, 0);
}

void PciReadFunction(
    _InOut_ PciNativeHeader_t* pciHeader, 
    _In_    PciHost_t*         host, 
    _In_    unsigned int       bus, 
    _In_    unsigned int       device, 
    _In_    unsigned int       function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t vendor = PciReadVendorId(host, bus, device, function);

	if (vendor && vendor != 0xFFFF) {
		/* Valid device! Okay, so the config space is 256 bytes long
		 * and we read in dwords: 64 reads should do it. */
		for (size_t i = 0; i < 64; i += 16) {
			*(uint32_t*)((size_t)pciHeader + i) = PciRead32(host, bus, device, function, i);
			*(uint32_t*)((size_t)pciHeader + i + 4) = PciRead32(host, bus, device, function, i + 4);
			*(uint32_t*)((size_t)pciHeader + i + 8) = PciRead32(host, bus, device, function, i + 8);
			*(uint32_t*)((size_t)pciHeader + i + 12) = PciRead32(host, bus, device, function, i + 12);
		}
	}
}

uint8_t PciReadSecondaryBusNumber(
    _In_ PciHost_t*   host,
    _In_ unsigned int bus, 
    _In_ unsigned int device, 
    _In_ unsigned int function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t vendor = PciReadVendorId(host, bus, device, function);

	if (vendor && vendor != 0xFFFF) {
		uint32_t offset = PciRead32(host, bus, device, function, 0x18);
		return (uint8_t)((offset >> 8) & 0xFF);
	} else {
		return 0xFF;
	}
}

uint8_t PciReadHeaderType(
    _In_ PciHost_t*   host,
    _In_ unsigned int bus,
    _In_ unsigned int device,
    _In_ unsigned int function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t vendor = PciReadVendorId(host, bus, device, function);

	if (vendor && vendor != 0xFFFF) {
		uint32_t offset = PciRead32(host, bus, device, function, 0x0C);
		return (uint8_t)((offset >> 16) & 0xFF);
	} else {
		return 0xFF;
	}
}
