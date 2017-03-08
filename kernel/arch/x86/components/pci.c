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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS X86 Bus Driver (IO)
 * - Enumerates the bus and registers the devices/controllers
 *   available in the system
 */

/* Includes 
 * - System */
#include <arch.h>
#include <pci.h>

/* Includes
 * - Library */
#include <stddef.h>

/* PCI I/O Helper
 * This function calculates the correct offset
 * based upon the pci bus type */
size_t PciCalculateOffset(int IsExtended,
	DevInfo_t Bus, DevInfo_t Device, DevInfo_t Function, size_t Register)
{
	/* PCI Express? */
	if (IsExtended) {
		return (size_t)((Bus << 20) | (Device << 15) | (Function << 12) | Register);
	}
	else {
		return (size_t)(0x80000000 | (Bus << 16) | (Device << 11) 
			| (Function << 8) | (Register & 0xFC));
	}
}

/* PciRead32
 * Reads a 32 bit value from the pci-bus
 * at the specified location bus, device, function and register */
uint32_t PciRead32(DevInfo_t Bus, DevInfo_t Device, DevInfo_t Function, size_t Register)
{
	outl(X86_PCI_SELECT, PciCalculateOffset(0, Bus, Device, Function, Register));
	return inl(X86_PCI_DATA);
}

/* PciRead16
 * Reads a 16 bit value from the pci-bus
 * at the specified location bus, device, function and register */
uint16_t PciRead16(DevInfo_t Bus, DevInfo_t Device, DevInfo_t Function, size_t Register)
{
	outl(X86_PCI_SELECT, PciCalculateOffset(0, Bus, Device, Function, Register));
	return inw(X86_PCI_DATA + (Register & 0x02));
}

/* PciRead8
 * Reads a 8 bit value from the pci-bus
 * at the specified location bus, device, function and register */
uint8_t PciRead8(DevInfo_t Bus, DevInfo_t Device, DevInfo_t Function, size_t Register)
{
	outl(X86_PCI_SELECT, PciCalculateOffset(0, Bus, Device, Function, Register));
	return inb(X86_PCI_DATA + (Register & 0x03));
}

/* PciWrite32
 * Writes a 32 bit value to the pci-bus
 * at the specified location bus, device, function and register */
void PciWrite32(DevInfo_t Bus, DevInfo_t Device,
	DevInfo_t Function, size_t Register, uint32_t Value)
{
	outl(X86_PCI_SELECT, PciCalculateOffset(0, Bus, Device, Function, Register));
	outl(X86_PCI_DATA, Value);
}

/* PciWrite16
 * Writes a 16 bit value to the pci-bus
 * at the specified location bus, device, function and register */
void PciWrite16(DevInfo_t Bus, DevInfo_t Device, 
	DevInfo_t Function, size_t Register, uint16_t Value)
{
	outl(X86_PCI_SELECT, PciCalculateOffset(0, Bus, Device, Function, Register));
	outw(X86_PCI_DATA + (Register & 0x02), Value);
}

/* PciWrite8
 * Writes a 8 bit value to the pci-bus
 * at the specified location bus, device, function and register */
void PciWrite8(DevInfo_t Bus, DevInfo_t Device, 
	DevInfo_t Function, size_t Register, uint8_t Value)
{
	outl(X86_PCI_SELECT, PciCalculateOffset(0, Bus, Device, Function, Register));
	outw(X86_PCI_DATA + (Register & 0x03), Value);
}

