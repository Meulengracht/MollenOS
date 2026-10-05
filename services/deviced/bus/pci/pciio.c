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
#include <firmware/fdt.h>
#include <stddef.h>

static size_t
__EcamOffset(
	_In_ PciHost_t*	  Io,
	_In_ unsigned int Bus,
	_In_ unsigned int Device,
	_In_ unsigned int Function,
	_In_ size_t 	  Register)
{
	return (size_t)(
        ((Bus - Io->BusStart) << 20) | 
            (Device << 15) | 
            (Function << 12) | 
            Register);
}

#ifdef __OSCONFIG_HAS_LEGACY_PCI
static void
__LegacySelect(
	_In_ PciHost_t*	  Io,
	_In_ unsigned int Bus,
	_In_ unsigned int Device,
	_In_ unsigned int Function,
	_In_ size_t 	  Register)
{
	size_t address = 0x80000000 | (Bus << 16) | (Device << 11) | (Function << 8) | (Register & 0xFC);
	WriteDeviceIo(&Io->IoSpace, PCI_REGISTER_SELECT, address, 4);
}

// Sub-dword accesses select a byte lane within the 32-bit data port.
#define __LEGACY_DATA_PORT(Register, Width) (PCI_REGISTER_DATA + ((Register) & 0x3 & ~((Width) - 1)))

static size_t
__LegacyRead(
		_In_ PciHost_t*    host,
		_In_ unsigned int bus,
		_In_ unsigned int slot,
		_In_ unsigned int function,
		_In_ size_t       reg,
		_In_ size_t       width)
{
	__LegacySelect(host, bus, slot, function, reg);
	return ReadDeviceIo(&host->IoSpace, __LEGACY_DATA_PORT(reg, width), width);
}

static void
__LegacyWrite(
		_In_ PciHost_t*    host,
		_In_ unsigned int bus,
		_In_ unsigned int slot,
		_In_ unsigned int function,
		_In_ size_t       reg,
		_In_ size_t       value,
		_In_ size_t       width)
{
	__LegacySelect(host, bus, slot, function, reg);
	WriteDeviceIo(&host->IoSpace, __LEGACY_DATA_PORT(reg, width), value, width);
}

const struct PciHostOperations g_pciLegacyOperations = {
	.Read = __LegacyRead,
	.Write = __LegacyWrite
};
#endif

static size_t
__EcamRead(
		_In_ PciHost_t*     host,
		_In_ unsigned int bus,
		_In_ unsigned int slot,
		_In_ unsigned int function,
		_In_ size_t       reg,
		_In_ size_t       width)
{
	return ReadDeviceIo(
        &host->IoSpace,
        __EcamOffset(host, bus, slot, function, reg),
        width
    );
}

static void
__EcamWrite(
		_In_ PciHost_t*     host,
		_In_ unsigned int bus,
		_In_ unsigned int slot,
		_In_ unsigned int function,
		_In_ size_t       reg,
		_In_ size_t       value,
		_In_ size_t       width)
{
	WriteDeviceIo(
        &host->IoSpace,
        __EcamOffset(host, bus, slot, function, reg),
        value,
        width
    );
}

static oserr_t
__DtTranslate(
		_In_ PciHost_t* host,
		_In_ uint32_t  space,
		_In_ uint64_t  address,
		_In_ uint64_t  length,
		_Out_ uint64_t* physicalOut)
{
	return FdtTranslatePciAddress(
        host->OpContext,
        space,
        address,
        length,
        physicalOut
    );
}

static oserr_t
__DtResolveInterrupt(
		_In_ PciHost_t*    host,
		_In_ unsigned int bus,
		_In_ unsigned int slot,
		_In_ unsigned int function,
		_In_ unsigned int pin,
		_Out_ int*        lineOut,
		_Out_ unsigned int* flagsOut)
{
	return FdtResolvePciInterrupt(
        host->OpContext,
        bus,
        slot,
        function,
        pin,
        lineOut,
        flagsOut
    );
}

const struct PciHostOperations g_pciAcpiEcamOperations = {
	.Read = __EcamRead,
	.Write = __EcamWrite
};

const struct PciHostOperations g_pciDtEcamOperations = {
	.Read = __EcamRead,
	.Write = __EcamWrite,
	.Translate = __DtTranslate,
	.ResolveInterrupt = __DtResolveInterrupt
};

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

	return bus >= (unsigned int)host->BusStart && bus <= (unsigned int)host->BusEnd &&
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
