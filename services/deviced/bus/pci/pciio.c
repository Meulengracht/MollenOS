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

static int
__ValidAccess(
		_In_ PciHost_t*    host,
		_In_ unsigned int bus,
		_In_ unsigned int slot,
		_In_ unsigned int function,
		_In_ size_t       reg,
		_In_ size_t       width)
{
	size_t limit = host->IsExtended ? 4096 : 256;

	// Keep invalid locations, out-of-range registers, and misaligned accesses
	// from reaching a host method or crossing its mapped configuration space.
	return bus >= (unsigned int)host->Identification.BusStart && bus <= (unsigned int)host->Identification.BusEnd &&
			slot < 32 && function < 8 && reg < limit && width <= limit - reg &&
			(reg & (width - 1)) == 0;
}

static size_t
__PciRead(
	_In_ PciHost_t*	  Io,
	_In_ unsigned int Bus,
	_In_ unsigned int Device,
	_In_ unsigned int Function,
	_In_ size_t 	  Register,
	_In_ size_t       Width)
{
	if (!__ValidAccess(Io, Bus, Device, Function, Register, Width)) {
		return (size_t)-1;
	}
	
    return Io->Operations->Read(Io, Bus, Device, Function, Register, Width);
}

static void
__PciWrite(
	_In_ PciHost_t*	  Io,
	_In_ unsigned int Bus,
	_In_ unsigned int Device,
	_In_ unsigned int Function,
	_In_ size_t 	  Register,
	_In_ size_t       Value,
	_In_ size_t       Width)
{
	if (!__ValidAccess(Io, Bus, Device, Function, Register, Width)) {
		return;
	}
	
    Io->Operations->Write(Io, Bus, Device, Function, Register, Value, Width);
}

uint32_t PciRead32(PciHost_t *Io, 
	unsigned int Bus, unsigned int Device, unsigned int Function, size_t Register)
{
	return (uint32_t)__PciRead(Io, Bus, Device, Function, Register, 4);
}

uint16_t PciRead16(PciHost_t *Io, 
	unsigned int Bus, unsigned int Device, unsigned int Function, size_t Register)
{
	return (uint16_t)__PciRead(Io, Bus, Device, Function, Register, 2);
}

uint8_t PciRead8(PciHost_t *Io, 
	unsigned int Bus, unsigned int Device, unsigned int Function, size_t Register)
{
	return (uint8_t)__PciRead(Io, Bus, Device, Function, Register, 1);
}

void PciWrite32(PciHost_t *Io, 
	unsigned int Bus, unsigned int Device, unsigned int Function, size_t Register, uint32_t Value)
{
	__PciWrite(Io, Bus, Device, Function, Register, Value, 4);
}

void PciWrite16(PciHost_t *Io, 
	unsigned int Bus, unsigned int Device, unsigned int Function, size_t Register, uint16_t Value)
{
	__PciWrite(Io, Bus, Device, Function, Register, Value, 2);
}

void PciWrite8(PciHost_t *Io, 
	unsigned int Bus, unsigned int Device, unsigned int Function, size_t Register, uint8_t Value)
{
	__PciWrite(Io, Bus, Device, Function, Register, Value, 1);
}

uint32_t PciDeviceRead(PciDevice_t *Device, size_t Register, size_t Length)
{
	if (Length == 1) {
		return (uint32_t)PciRead8(Device->Host, Device->Bus, Device->Slot, Device->Function, Register);
	} else if (Length == 2) {
		return (uint32_t)PciRead16(Device->Host, Device->Bus, Device->Slot, Device->Function, Register);
	} else {
		return PciRead32(Device->Host, Device->Bus, Device->Slot, Device->Function, Register);
	}
}

void PciDeviceWrite(PciDevice_t *Device, size_t Register, uint32_t Value, size_t Length)
{
	if (Length == 1) {
		PciWrite8(Device->Host, Device->Bus, Device->Slot, Device->Function, Register, (uint8_t)(Value & 0xFF));
	} else if (Length == 2) {
		PciWrite16(Device->Host, Device->Bus, Device->Slot, Device->Function, Register, (uint16_t)(Value & 0xFFFFF));
	} else {
		PciWrite32(Device->Host, Device->Bus, Device->Slot, Device->Function, Register, Value);
	}
}
