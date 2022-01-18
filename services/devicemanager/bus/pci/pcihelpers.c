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

#include "bus.h"

/* PciReadVendorId
 * Reads the vendor id at given bus/device/function location */
uint16_t PciReadVendorId(PciBus_t *BusIo, unsigned int Bus, unsigned int Device, unsigned int Function)
{
	return PciRead16(BusIo, Bus, Device, Function, 0);
}

/* PciReadFunction
 * Reads in the pci header that exists at the given location
 * and fills out the information into <Pcs> */
void PciReadFunction(PciNativeHeader_t *Pcs, 
	PciBus_t *BusIo, unsigned int Bus, unsigned int Device, unsigned int Function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t Vendor = PciReadVendorId(BusIo, Bus, Device, Function);
	size_t i;

	if (Vendor && Vendor != 0xFFFF) {
		/* Valid device! Okay, so the config space is 256 bytes long
		 * and we read in dwords: 64 reads should do it. */
		for (i = 0; i < 64; i += 16) {
			*(uint32_t*)((size_t)Pcs + i) = PciRead32(BusIo, Bus, Device, Function, i);
			*(uint32_t*)((size_t)Pcs + i + 4) = PciRead32(BusIo, Bus, Device, Function, i + 4);
			*(uint32_t*)((size_t)Pcs + i + 8) = PciRead32(BusIo, Bus, Device, Function, i + 8);
			*(uint32_t*)((size_t)Pcs + i + 12) = PciRead32(BusIo, Bus, Device, Function, i + 12);
		}
	}
}

/* PciReadSecondaryBusNumber
 * Reads the secondary bus number at given pci device location
 * we can use this to get the bus-number behind a bridge */
uint8_t PciReadSecondaryBusNumber(PciBus_t *BusIo, unsigned int Bus, unsigned int Device, unsigned int Function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t Vendor = PciReadVendorId(BusIo, Bus, Device, Function);

	if (Vendor && Vendor != 0xFFFF) {
		uint32_t offset = PciRead32(BusIo, Bus, Device, Function, 0x18);
		return (uint8_t)((offset >> 8) & 0xFF);
	}
	else {
		return 0xFF;
	}
}

/* Reads the sub class at given location
 * Bit 7 - MultiFunction, Lower 4 bits is type.
 * Type 0 is standard, Type 1 is PCI-PCI Bridge,
 * Type 2 is CardBus Bridge */
uint8_t PciReadHeaderType(PciBus_t *BusIo, unsigned int Bus, unsigned int Device, unsigned int Function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t Vendor = PciReadVendorId(BusIo, Bus, Device, Function);

	if (Vendor && Vendor != 0xFFFF) {
		uint32_t offset = PciRead32(BusIo, Bus, Device, Function, 0x0C);
		return (uint8_t)((offset >> 16) & 0xFF);
	}
	else {
		return 0xFF;
	}
}
