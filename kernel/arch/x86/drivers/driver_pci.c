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
#include <arch.h>
#include <pci.h>
#include <stddef.h>
#include <stdio.h>
#include <limits.h>

/* PCI Interface I/O */
uint32_t pci_read_dword(const uint16_t bus, const uint16_t dev,
	const uint16_t func, const uint32_t reg)
{
	/* Select Bus/Device/Function/Register */
	outl(X86_PCI_SELECT, 0x80000000L | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
		((uint32_t)func << 8) | (reg & ~3));

	/* Read Data */
	return inl(X86_PCI_DATA + (reg & 3));
}

uint16_t pci_read_word(const uint16_t bus, const uint16_t dev,
	const uint16_t func, const uint32_t reg)
{
	/* Select Bus/Device/Function/Register */
	outl(X86_PCI_SELECT, 0x80000000L | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
		((uint32_t)func << 8) | (reg & ~3));

	/* Read Data */
	return inw(X86_PCI_DATA + (reg & 3));
}

uint8_t pci_read_byte(const uint16_t bus, const uint16_t dev,
	const uint16_t func, const uint32_t reg)
{
	/* Select Bus/Device/Function/Register */
	outl(X86_PCI_SELECT, 0x80000000L | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
		((uint32_t)func << 8) | (reg & ~3));

	/* Read Data */
	return inb(X86_PCI_DATA + (reg & 3));
}

void pci_write_dword(const uint16_t bus, const uint16_t dev,
	const uint16_t func, const uint32_t reg, uint32_t value)
{
	/* Select Bus/Device/Function/Register */
	outl(X86_PCI_SELECT, 0x80000000L | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
		((uint32_t)func << 8) | (reg & ~3));

	/* Write DATA */
	outl(X86_PCI_DATA + (reg & 3), value);
	return;
}

void pci_write_word(const uint16_t bus, const uint16_t dev,
	const uint16_t func, const uint32_t reg, uint16_t value)
{
	/* Select Bus/Device/Function/Register */
	outl(X86_PCI_SELECT, 0x80000000L | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
		((uint32_t)func << 8) | (reg & ~3));

	/* Write DATA */
	outw(X86_PCI_DATA + (reg & 3), value);
	return;
}

void pci_write_byte(const uint16_t bus, const uint16_t dev,
	const uint16_t func, const uint32_t reg, uint8_t value)
{
	/* Select Bus/Device/Function/Register */
	outl(X86_PCI_SELECT, 0x80000000L | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
		((uint32_t)func << 8) | (reg & ~3));

	/* Write DATA */
	outb(X86_PCI_DATA + (reg & 3), value);
	return;
}

/* Reads the vendor id at given location */
uint16_t pci_read_vendor_id(const uint16_t bus, const uint16_t device, const uint16_t function)
{
	/* Get the dword and parse the vendor and device ID */
	uint32_t tmp = pci_read_dword(bus, device, function, 0);
	uint16_t vendor = (tmp & 0xFFFF);

	return vendor;
}

/* Reads a PCI header at given location */
void pci_read_function(pci_device_header_t *pcs, const uint16_t bus, const uint16_t device, const uint16_t function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t vendor = pci_read_vendor_id(bus, device, function);
	uint32_t i;

	if (vendor && vendor != 0xFFFF)
	{
		/* Valid device! Okay, so the config space is 256 bytes long
		* and we read in dwords: 64 reads should do it.
		*/

		for (i = 0; i < 64; i += 16)
		{
			*(uint32_t*)((size_t)pcs + i) = pci_read_dword(bus, device, function, i);
			*(uint32_t*)((size_t)pcs + i + 4) = pci_read_dword(bus, device, function, i + 4);
			*(uint32_t*)((size_t)pcs + i + 8) = pci_read_dword(bus, device, function, i + 8);
			*(uint32_t*)((size_t)pcs + i + 12) = pci_read_dword(bus, device, function, i + 12);
		}
	}
}

/* Reads the base class at given location */
uint8_t pci_read_base_class(const uint16_t bus, const uint16_t device, const uint16_t function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t vendor = pci_read_vendor_id(bus, device, function);

	if (vendor && vendor != 0xFFFF)
	{
		/* Valid device! Okay, so read the base_class
		*/
		uint32_t offset = pci_read_dword(bus, device, function, 0x08);
		return (uint8_t)((offset >> 24) & 0xFF);
	}
	else
		return 0xFF;
}

/* Reads the sub class at given location */
uint8_t pci_read_sub_class(const uint16_t bus, const uint16_t device, const uint16_t function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t vendor = pci_read_vendor_id(bus, device, function);

	if (vendor && vendor != 0xFFFF)
	{
		/* Valid device! Okay, so read the base_class
		*/
		uint32_t offset = pci_read_dword(bus, device, function, 0x08);
		return (uint8_t)((offset >> 16) & 0xFF);
	}
	else
		return 0xFF;
}

/* Reads the secondary bus number at given location */
uint8_t pci_read_secondary_bus_number(const uint16_t bus, const uint16_t device, const uint16_t function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t vendor = pci_read_vendor_id(bus, device, function);

	if (vendor && vendor != 0xFFFF)
	{
		/* Valid device! Okay, so read the base_class
		*/
		uint32_t offset = pci_read_dword(bus, device, function, 0x18);
		return (uint8_t)((offset >> 8) & 0xFF);
	}
	else
		return 0xFF;
}

/* Reads the sub class at given location */
/* Bit 7 - MultiFunction, Lower 4 bits is type.
* Type 0 is standard, Type 1 is PCI-PCI Bridge,
* Type 2 is CardBus Bridge */
uint8_t pci_read_header_type(const uint16_t bus, const uint16_t device, const uint16_t function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t vendor = pci_read_vendor_id(bus, device, function);

	if (vendor && vendor != 0xFFFF)
	{
		/* Valid device! Okay, so read the base_class
		*/
		uint32_t offset = pci_read_dword(bus, device, function, 0x0C);
		return (uint8_t)((offset >> 16) & 0xFF);
	}
	else
		return 0xFF;
}
