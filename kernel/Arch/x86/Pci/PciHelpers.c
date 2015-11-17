/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* along with this program.If not, see <http://www.gnu.org/licenses/>.
*
*
* MollenOS X86 Driver PCI Helpers
*/

/* Includes */
#include <Arch.h>
#include <Pci.h>

/* Reads the vendor id at given location */
uint16_t PciReadVendorId(uint32_t Bus, uint32_t Device, uint32_t Function)
{
	/* Get the dword and parse the vendor and device ID */
	uint32_t tmp = PciRead32(Bus, Device, Function, 0);
	return (uint16_t)(tmp & 0xFFFF);
}

/* Reads a PCI header at given location */
void PciReadFunction(PciNativeHeader_t *Pcs, uint32_t Bus, uint32_t Device, uint32_t Function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t vendor = PciReadVendorId(Bus, Device, Function);
	uint32_t i;

	if (vendor && vendor != 0xFFFF)
	{
		/* Valid device! Okay, so the config space is 256 bytes long
		* and we read in dwords: 64 reads should do it.
		*/

		for (i = 0; i < 64; i += 16)
		{
			*(uint32_t*)((size_t)Pcs + i) = PciRead32(Bus, Device, Function, i);
			*(uint32_t*)((size_t)Pcs + i + 4) = PciRead32(Bus, Device, Function, i + 4);
			*(uint32_t*)((size_t)Pcs + i + 8) = PciRead32(Bus, Device, Function, i + 8);
			*(uint32_t*)((size_t)Pcs + i + 12) = PciRead32(Bus, Device, Function, i + 12);
		}
	}
}

/* Reads the base class at given location */
uint8_t PciReadBaseClass(uint32_t Bus, uint32_t Device, uint32_t Function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t vendor = PciReadVendorId(Bus, Device, Function);

	if (vendor && vendor != 0xFFFF)
	{
		/* Valid device! Okay, so read the base_class */
		uint32_t offset = PciRead32(Bus, Device, Function, 0x08);
		return (uint8_t)((offset >> 24) & 0xFF);
	}
	else
		return 0xFF;
}

/* Reads the sub class at given location */
uint8_t PciReadSubclass(uint32_t Bus, uint32_t Device, uint32_t Function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t vendor = PciReadVendorId(Bus, Device, Function);

	if (vendor && vendor != 0xFFFF)
	{
		/* Valid device! Okay, so read the base_class */
		uint32_t offset = PciRead32(Bus, Device, Function, 0x08);
		return (uint8_t)((offset >> 16) & 0xFF);
	}
	else
		return 0xFF;
}

/* Reads the secondary bus number at given location */
uint8_t PciReadSecondaryBusNumber(uint32_t Bus, uint32_t Device, uint32_t Function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t vendor = PciReadVendorId(Bus, Device, Function);

	if (vendor && vendor != 0xFFFF)
	{
		/* Valid device! Okay, so read the base_class */
		uint32_t offset = PciRead32(Bus, Device, Function, 0x18);
		return (uint8_t)((offset >> 8) & 0xFF);
	}
	else
		return 0xFF;
}

/* Reads the sub class at given location */
/* Bit 7 - MultiFunction, Lower 4 bits is type.
* Type 0 is standard, Type 1 is PCI-PCI Bridge,
* Type 2 is CardBus Bridge */
uint8_t PciReadHeaderType(uint32_t Bus, uint32_t Device, uint32_t Function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t vendor = PciReadVendorId(Bus, Device, Function);

	if (vendor && vendor != 0xFFFF)
	{
		/* Valid device! Okay, so read the base_class */
		uint32_t offset = PciRead32(Bus, Device, Function, 0x0C);
		return (uint8_t)((offset >> 16) & 0xFF);
	}
	else
		return 0xFF;
}
