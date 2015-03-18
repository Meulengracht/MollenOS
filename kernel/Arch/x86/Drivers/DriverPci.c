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
* MollenOS X86-32 Driver PCI I/O
*/

/* Includes */
#include <Arch.h>
#include <Pci.h>
#include <stddef.h>
#include <stdio.h>
#include <limits.h>

/* PCI Interface I/O */
uint32_t PciReadDword(const uint16_t Bus, const uint16_t Device,
					  const uint16_t Function, const uint32_t Register)
{
	/* Select Bus/Device/Function/Register */
	outl(X86_PCI_SELECT, 0x80000000
		| (Bus << 16)
		| (Device << 11)
		| (Function << 8)
		| (Register & 0xFC));

	/* Read Data */
	return inl(X86_PCI_DATA + (Register & 3));
}

uint16_t PciReadWord(const uint16_t Bus, const uint16_t Device,
					 const uint16_t Function, const uint32_t Register)
{
	/* Select Bus/Device/Function/Register */
	outl(X86_PCI_SELECT, 0x80000000
		| (Bus << 16)
		| (Device << 11)
		| (Function << 8)
		| (Register & 0xFC));

	/* Read Data */
	return inw(X86_PCI_DATA + (Register & 3));
}

uint8_t PciReadByte(const uint16_t Bus, const uint16_t Device,
					const uint16_t Function, const uint32_t Register)
{
	/* Select Bus/Device/Function/Register */
	outl(X86_PCI_SELECT, 0x80000000
		| (Bus << 16)
		| (Device << 11)
		| (Function << 8)
		| (Register & 0xFC));

	/* Read Data */
	return inb(X86_PCI_DATA + (Register & 3));
}

/* Write functions */
void PciWriteDword(const uint16_t Bus, const uint16_t Device,
				   const uint16_t Function, const uint32_t Register, uint32_t Value)
{
	/* Select Bus/Device/Function/Register */
	outl(X86_PCI_SELECT, 0x80000000
		| (Bus << 16)
		| (Device << 11)
		| (Function << 8)
		| (Register & 0xFC));

	/* Write DATA */
	outl(X86_PCI_DATA, Value);
}

void PciWriteWord(const uint16_t Bus, const uint16_t Device,
				  const uint16_t Function, const uint32_t Register, uint16_t Value)
{
	/* Select Bus/Device/Function/Register */
	outl(X86_PCI_SELECT, 0x80000000
		| (Bus << 16)
		| (Device << 11)
		| (Function << 8)
		| (Register & 0xFC));

	/* Write DATA */
	outw(X86_PCI_DATA, Value);
}

void PciWriteByte(const uint16_t Bus, const uint16_t Device,
				  const uint16_t Function, const uint32_t Register, uint8_t Value)
{
	/* Select Bus/Device/Function/Register */
	outl(X86_PCI_SELECT, 0x80000000
		| (Bus << 16)
		| (Device << 11)
		| (Function << 8)
		| (Register & 0xFC));

	/* Write DATA */
	outb(X86_PCI_DATA + (Register & 0x03), Value);
}

/* Reads the vendor id at given location */
uint16_t PciReadVendorId(const uint16_t Bus, const uint16_t Device, const uint16_t Function)
{
	/* Get the dword and parse the vendor and device ID */
	uint32_t tmp = PciReadDword(Bus, Device, Function, 0);
	return (uint16_t)(tmp & 0xFFFF);
}

/* Reads a PCI header at given location */
void PciReadFunction(PciNativeHeader_t *Pcs, const uint16_t Bus, const uint16_t Device, const uint16_t Function)
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
			*(uint32_t*)((size_t)Pcs + i) = PciReadDword(Bus, Device, Function, i);
			*(uint32_t*)((size_t)Pcs + i + 4) = PciReadDword(Bus, Device, Function, i + 4);
			*(uint32_t*)((size_t)Pcs + i + 8) = PciReadDword(Bus, Device, Function, i + 8);
			*(uint32_t*)((size_t)Pcs + i + 12) = PciReadDword(Bus, Device, Function, i + 12);
		}
	}
}

/* Reads the base class at given location */
uint8_t PciReadBaseClass(const uint16_t Bus, const uint16_t Device, const uint16_t Function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t vendor = PciReadVendorId(Bus, Device, Function);

	if (vendor && vendor != 0xFFFF)
	{
		/* Valid device! Okay, so read the base_class
		*/
		uint32_t offset = PciReadDword(Bus, Device, Function, 0x08);
		return (uint8_t)((offset >> 24) & 0xFF);
	}
	else
		return 0xFF;
}

/* Reads the sub class at given location */
uint8_t PciReadSubclass(const uint16_t Bus, const uint16_t Device, const uint16_t Function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t vendor = PciReadVendorId(Bus, Device, Function);

	if (vendor && vendor != 0xFFFF)
	{
		/* Valid device! Okay, so read the base_class
		*/
		uint32_t offset = PciReadDword(Bus, Device, Function, 0x08);
		return (uint8_t)((offset >> 16) & 0xFF);
	}
	else
		return 0xFF;
}

/* Reads the secondary bus number at given location */
uint8_t PciReadSecondaryBusNumber(const uint16_t Bus, const uint16_t Device, const uint16_t Function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t vendor = PciReadVendorId(Bus, Device, Function);

	if (vendor && vendor != 0xFFFF)
	{
		/* Valid device! Okay, so read the base_class
		*/
		uint32_t offset = PciReadDword(Bus, Device, Function, 0x18);
		return (uint8_t)((offset >> 8) & 0xFF);
	}
	else
		return 0xFF;
}

/* Reads the sub class at given location */
/* Bit 7 - MultiFunction, Lower 4 bits is type.
* Type 0 is standard, Type 1 is PCI-PCI Bridge,
* Type 2 is CardBus Bridge */
uint8_t PciReadHeaderType(const uint16_t Bus, const uint16_t Device, const uint16_t Function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t vendor = PciReadVendorId(Bus, Device, Function);

	if (vendor && vendor != 0xFFFF)
	{
		/* Valid device! Okay, so read the base_class
		*/
		uint32_t offset = PciReadDword(Bus, Device, Function, 0x0C);
		return (uint8_t)((offset >> 16) & 0xFF);
	}
	else
		return 0xFF;
}
