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
#include "bus.h"

/* Includes
 * - Library */
#include <stddef.h>

/* PCI I/O Helper
 * This function calculates the correct offset
 * based upon the pci bus type */
size_t PciCalculateOffset(PciBus_t *Io,
	DevInfo_t Bus, DevInfo_t Device, DevInfo_t Function, size_t Register)
{
	/* PCI Express? */
	if (Io->IsExtended) {
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
uint32_t PciRead32(PciBus_t *Io, 
	DevInfo_t Bus, DevInfo_t Device, DevInfo_t Function, size_t Register)
{
	if (Io != NULL
		&& Io->IsExtended) {
		return IoSpaceRead(Io->IoSpace, PciCalculateOffset(Io, Bus, Device, Function, Register), 4);
	}
	else {
		IoSpaceWrite(Io->IoSpace, PCI_REGISTER_SELECT,
			PciCalculateOffset(Io, Bus, Device, Function, Register), 4);
		return IoSpaceRead(Io->IoSpace, PCI_REGISTER_DATA, 4);
	}
}

/* PciRead16
 * Reads a 16 bit value from the pci-bus
 * at the specified location bus, device, function and register */
uint16_t PciRead16(PciBus_t *Io, 
	DevInfo_t Bus, DevInfo_t Device, DevInfo_t Function, size_t Register)
{
	if (Io != NULL
		&& Io->IsExtended) {
		return IoSpaceRead(Io->IoSpace, PciCalculateOffset(Io, Bus, Device, Function, Register), 2);
	}
	else {
		IoSpaceWrite(Io->IoSpace, PCI_REGISTER_SELECT,
			PciCalculateOffset(Io, Bus, Device, Function, Register), 4);
		return IoSpaceRead(Io->IoSpace, PCI_REGISTER_DATA + (Register & 0x02), 2);
	}
}

/* PciRead8
 * Reads a 8 bit value from the pci-bus
 * at the specified location bus, device, function and register */
uint8_t PciRead8(PciBus_t *Io, 
	DevInfo_t Bus, DevInfo_t Device, DevInfo_t Function, size_t Register)
{
	if (Io != NULL
		&& Io->IsExtended) {
		return IoSpaceRead(Io->IoSpace, PciCalculateOffset(Io, Bus, Device, Function, Register), 1);
	}
	else {
		IoSpaceWrite(Io->IoSpace, PCI_REGISTER_SELECT,
			PciCalculateOffset(Io, Bus, Device, Function, Register), 4);
		return IoSpaceRead(Io->IoSpace, PCI_REGISTER_DATA + (Register & 0x03), 1);
	}
}

/* PciWrite32
 * Writes a 32 bit value to the pci-bus
 * at the specified location bus, device, function and register */
void PciWrite32(PciBus_t *Io, 
	DevInfo_t Bus, DevInfo_t Device, DevInfo_t Function, size_t Register, uint32_t Value)
{
	if (Io != NULL
		&& Io->IsExtended) {
		IoSpaceWrite(Io->IoSpace, PciCalculateOffset(Io, Bus, Device, Function, Register), Value, 4);
	}
	else {
		IoSpaceWrite(Io->IoSpace, PCI_REGISTER_SELECT,
			PciCalculateOffset(Io, Bus, Device, Function, Register), 4);
		IoSpaceWrite(Io->IoSpace, PCI_REGISTER_DATA, Value, 4);
	}
}

/* PciWrite16
 * Writes a 16 bit value to the pci-bus
 * at the specified location bus, device, function and register */
void PciWrite16(PciBus_t *Io, 
	DevInfo_t Bus, DevInfo_t Device, DevInfo_t Function, size_t Register, uint16_t Value)
{
	if (Io != NULL
		&& Io->IsExtended) {
		IoSpaceWrite(Io->IoSpace, PciCalculateOffset(Io, Bus, Device, Function, Register), Value, 2);
	}
	else {
		IoSpaceWrite(Io->IoSpace, PCI_REGISTER_SELECT,
			PciCalculateOffset(Io, Bus, Device, Function, Register), 4);
		IoSpaceWrite(Io->IoSpace, PCI_REGISTER_DATA + (Register & 0x02), Value, 2);
	}
}

/* PciWrite8
 * Writes a 8 bit value to the pci-bus
 * at the specified location bus, device, function and register */
void PciWrite8(PciBus_t *Io, 
	DevInfo_t Bus, DevInfo_t Device, DevInfo_t Function, size_t Register, uint8_t Value)
{
	if (Io != NULL
		&& Io->IsExtended) {
		IoSpaceWrite(Io->IoSpace, PciCalculateOffset(Io, Bus, Device, Function, Register), Value, 1);
	}
	else {
		IoSpaceWrite(Io->IoSpace, PCI_REGISTER_SELECT,
			PciCalculateOffset(Io, Bus, Device, Function, Register), 4);
		IoSpaceWrite(Io->IoSpace, PCI_REGISTER_DATA + (Register & 0x03), Value, 1);
	}
}

/* PciDeviceRead
 * Reads a value of the given length from the given register
 * and this function takes care of the rest */
uint32_t PciDeviceRead(PciDevice_t *Device, size_t Register, size_t Length)
{
	if (Length == 1) {
		return (uint32_t)PciRead8(Device->BusIo, Device->Bus, Device->Device, Device->Function, Register);
	}
	else if (Length == 2) {
		return (uint32_t)PciRead16(Device->BusIo, Device->Bus, Device->Device, Device->Function, Register);
	}
	else {
		return PciRead32(Device->BusIo, Device->Bus, Device->Device, Device->Function, Register);
	}
}

/* PciDeviceWrite
 * Writes a value of the given length to the given register
 * and this function takes care of the rest */
void PciDeviceWrite(PciDevice_t *Device, size_t Register, uint32_t Value, size_t Length)
{
	if (Length == 1) {
		PciWrite8(Device->BusIo, Device->Bus, Device->Device, Device->Function, Register, (uint8_t)(Value & 0xFF));
	}
	else if (Length == 2) {
		PciWrite16(Device->BusIo, Device->Bus, Device->Device, Device->Function, Register, (uint16_t)(Value & 0xFFFFF));
	}
	else {
		PciWrite32(Device->BusIo, Device->Bus, Device->Device, Device->Function, Register, Value);
	}
}
