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
* MollenOS X86 Driver PCI I/O
*/

/* Includes */
#include <Arch.h>
#include <Pci.h>

/* PCI Interface I/O */
uint32_t PciRead32(PciBus_t *BusIo, 
	uint32_t Bus, uint32_t Device, uint32_t Function, uint32_t Register)
{
	if (BusIo->IsExtended) {
		return IoSpaceRead(BusIo->IoSpace, 
			((Bus << 20) | (Device << 15)
			| (Function << 12)
			| Register), 4);
	}
	else
	{
		/* Select Bus/Device/Function/Register */
		outl(X86_PCI_SELECT, 0x80000000
			| (Bus << 16)
			| (Device << 11)
			| (Function << 8)
			| (Register & 0xFC));

		/* Read Data */
		return inl(X86_PCI_DATA);
	}
}

uint16_t PciRead16(PciBus_t *BusIo, 
	uint32_t Bus, uint32_t Device, uint32_t Function, uint32_t Register)
{
	if (BusIo->IsExtended) {
		return (uint16_t)IoSpaceRead(BusIo->IoSpace,
			((Bus << 20) | (Device << 15)
			| (Function << 12)
			| Register), 2);
	}
	else
	{
		/* Select Bus/Device/Function/Register */
		outl(X86_PCI_SELECT, 0x80000000
			| (Bus << 16)
			| (Device << 11)
			| (Function << 8)
			| (Register & 0xFC));

		/* Read Data */
		return inw(X86_PCI_DATA + (Register & 0x2));
	}
}

uint8_t PciRead8(PciBus_t *BusIo, uint32_t Bus, uint32_t Device, uint32_t Function, uint32_t Register)
{
	if (BusIo->IsExtended) {
		return (uint8_t)IoSpaceRead(BusIo->IoSpace,
			((Bus << 20) | (Device << 15)
			| (Function << 12)
			| Register), 1);
	}
	else
	{
		/* Select Bus/Device/Function/Register */
		outl(X86_PCI_SELECT, 0x80000000
			| (Bus << 16)
			| (Device << 11)
			| (Function << 8)
			| (Register & 0xFC));

		/* Read Data */
		return inb(X86_PCI_DATA + (Register & 0x3));
	}
}

/* Write functions */
void PciWrite32(PciBus_t *BusIo, uint32_t Bus, uint32_t Device, uint32_t Function, uint32_t Register, uint32_t Value)
{
	if (BusIo->IsExtended) {
		IoSpaceWrite(BusIo->IoSpace,
			((Bus << 20) | (Device << 15)
			| (Function << 12)
			| Register), Value, 4);
	}
	else
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
}

void PciWrite16(PciBus_t *BusIo, uint32_t Bus, uint32_t Device, uint32_t Function, uint32_t Register, uint16_t Value)
{
	if (BusIo->IsExtended) {
		IoSpaceWrite(BusIo->IoSpace,
			((Bus << 20) | (Device << 15)
			| (Function << 12)
			| Register), Value, 2);
	}
	else
	{
		/* Select Bus/Device/Function/Register */
		outl(X86_PCI_SELECT, 0x80000000
			| (Bus << 16)
			| (Device << 11)
			| (Function << 8)
			| (Register & 0xFC));

		/* Write DATA */
		outw(X86_PCI_DATA + (Register & 0x2), Value);
	}
}

void PciWrite8(PciBus_t *BusIo, uint32_t Bus, uint32_t Device, uint32_t Function, uint32_t Register, uint8_t Value)
{
	if (BusIo->IsExtended) {
		IoSpaceWrite(BusIo->IoSpace,
			((Bus << 20) | (Device << 15)
			| (Function << 12)
			| Register), Value, 1);
	}
	else
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
}

/* Helpers */
uint32_t PciDeviceRead(PciDevice_t *Device, uint32_t Register, uint32_t Length)
{
	if (Length == 1)
		return (uint32_t)PciRead8(Device->PciBus, Device->Bus, Device->Device, Device->Function, Register);
	else if (Length == 2)
		return (uint32_t)PciRead16(Device->PciBus, Device->Bus, Device->Device, Device->Function, Register);
	else
		return PciRead32(Device->PciBus, Device->Bus, Device->Device, Device->Function, Register);
}

void PciDeviceWrite(PciDevice_t *Device, uint32_t Register, uint32_t Value, uint32_t Length)
{
	if (Length == 1)
		PciWrite8(Device->PciBus, Device->Bus, Device->Device, Device->Function, Register, (uint8_t)(Value & 0xFF));
	else if (Length == 2)
		PciWrite16(Device->PciBus, Device->Bus, Device->Device, Device->Function, Register, (uint16_t)(Value & 0xFFFFF));
	else
		PciWrite32(Device->PciBus, Device->Bus, Device->Device, Device->Function, Register, Value);
}
