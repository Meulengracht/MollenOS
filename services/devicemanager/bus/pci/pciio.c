/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * MollenOS X86 Bus Driver (IO)
 * - Enumerates the bus and registers the devices/controllers
 *   available in the system
 */

#include "bus.h"
#include <stddef.h>

size_t
PciCalculateOffset(
	_In_ PciBus_t*	  Io,
	_In_ unsigned int Bus,
	_In_ unsigned int Device,
	_In_ unsigned int Function,
	_In_ size_t 	  Register)
{
	if (Io->IsExtended) {
		return (size_t)(((Bus - Io->BusStart) << 20) | (Device << 15) | (Function << 12) | Register);
	}
	else {
		return (size_t)(0x80000000 | (Bus << 16) | (Device << 11) | (Function << 8) | (Register & 0xFC));
	}
}

uint32_t PciRead32(PciBus_t *Io, 
	unsigned int Bus, unsigned int Device, unsigned int Function, size_t Register)
{
	if (Io && Io->IsExtended) {
		return ReadDeviceIo(&Io->IoSpace, PciCalculateOffset(Io, Bus, Device, Function, Register), 4);
	}
	else {
		WriteDeviceIo(&Io->IoSpace, PCI_REGISTER_SELECT, PciCalculateOffset(Io, Bus, Device, Function, Register), 4);
		return ReadDeviceIo(&Io->IoSpace, PCI_REGISTER_DATA, 4);
	}
}

uint16_t PciRead16(PciBus_t *Io, 
	unsigned int Bus, unsigned int Device, unsigned int Function, size_t Register)
{
	if (Io && Io->IsExtended) {
		return (uint16_t)ReadDeviceIo(&Io->IoSpace, PciCalculateOffset(Io, Bus, Device, Function, Register), 2);
	}
	else {
		WriteDeviceIo(&Io->IoSpace, PCI_REGISTER_SELECT, PciCalculateOffset(Io, Bus, Device, Function, Register), 4);
		return (uint16_t)ReadDeviceIo(&Io->IoSpace, PCI_REGISTER_DATA + (Register & 0x02), 2);
	}
}

uint8_t PciRead8(PciBus_t *Io, 
	unsigned int Bus, unsigned int Device, unsigned int Function, size_t Register)
{
	if (Io && Io->IsExtended) {
		return (uint8_t)ReadDeviceIo(&Io->IoSpace, PciCalculateOffset(Io, Bus, Device, Function, Register), 1);
	}
	else {
		WriteDeviceIo(&Io->IoSpace, PCI_REGISTER_SELECT, PciCalculateOffset(Io, Bus, Device, Function, Register), 4);
		return (uint8_t)ReadDeviceIo(&Io->IoSpace, PCI_REGISTER_DATA + (Register & 0x03), 1);
	}
}

void PciWrite32(PciBus_t *Io, 
	unsigned int Bus, unsigned int Device, unsigned int Function, size_t Register, uint32_t Value)
{
	if (Io && Io->IsExtended) {
		WriteDeviceIo(&Io->IoSpace, PciCalculateOffset(Io, Bus, Device, Function, Register), Value, 4);
	}
	else {
		WriteDeviceIo(&Io->IoSpace, PCI_REGISTER_SELECT, PciCalculateOffset(Io, Bus, Device, Function, Register), 4);
		WriteDeviceIo(&Io->IoSpace, PCI_REGISTER_DATA, Value, 4);
	}
}

void PciWrite16(PciBus_t *Io, 
	unsigned int Bus, unsigned int Device, unsigned int Function, size_t Register, uint16_t Value)
{
	if (Io && Io->IsExtended) {
		WriteDeviceIo(&Io->IoSpace, PciCalculateOffset(Io, Bus, Device, Function, Register), Value, 2);
	}
	else {
		WriteDeviceIo(&Io->IoSpace, PCI_REGISTER_SELECT, PciCalculateOffset(Io, Bus, Device, Function, Register), 4);
		WriteDeviceIo(&Io->IoSpace, PCI_REGISTER_DATA + (Register & 0x02), Value, 2);
	}
}

void PciWrite8(PciBus_t *Io, 
	unsigned int Bus, unsigned int Device, unsigned int Function, size_t Register, uint8_t Value)
{
	if (Io && Io->IsExtended) {
		WriteDeviceIo(&Io->IoSpace, PciCalculateOffset(Io, Bus, Device, Function, Register), Value, 1);
	}
	else {
		WriteDeviceIo(&Io->IoSpace, PCI_REGISTER_SELECT, PciCalculateOffset(Io, Bus, Device, Function, Register), 4);
		WriteDeviceIo(&Io->IoSpace, PCI_REGISTER_DATA + (Register & 0x03), Value, 1);
	}
}

uint32_t PciDeviceRead(PciDevice_t *Device, size_t Register, size_t Length)
{
	if (Length == 1) {
		return (uint32_t)PciRead8(Device->BusIo, Device->Bus, Device->Slot, Device->Function, Register);
	}
	else if (Length == 2) {
		return (uint32_t)PciRead16(Device->BusIo, Device->Bus, Device->Slot, Device->Function, Register);
	}
	else {
		return PciRead32(Device->BusIo, Device->Bus, Device->Slot, Device->Function, Register);
	}
}

void PciDeviceWrite(PciDevice_t *Device, size_t Register, uint32_t Value, size_t Length)
{
	if (Length == 1) {
		PciWrite8(Device->BusIo, Device->Bus, Device->Slot, Device->Function, Register, (uint8_t)(Value & 0xFF));
	}
	else if (Length == 2) {
		PciWrite16(Device->BusIo, Device->Bus, Device->Slot, Device->Function, Register, (uint16_t)(Value & 0xFFFFF));
	}
	else {
		PciWrite32(Device->BusIo, Device->Bus, Device->Slot, Device->Function, Register, Value);
	}
}
