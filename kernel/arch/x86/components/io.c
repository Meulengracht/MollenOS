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
#define __MODULE "IOLL"

/* Includes 
 * - System */
#include <system/io.h>
#include <debug.h>
#include <arch.h>
#include <pci.h>

/* Includes
 * - Library */
#include <stddef.h>

__EXTERN uint8_t inb(uint16_t port);
__EXTERN uint16_t inw(uint16_t port);
__EXTERN uint32_t inl(uint16_t port);
__EXTERN void outb(uint16_t port, uint8_t data);
__EXTERN void outw(uint16_t port, uint16_t data);
__EXTERN void outl(uint16_t port, uint32_t data);

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

/* IoRead 
 * Reads a value from the given data source. Accepted values in
 * width are 1, 2, 4 or 8. */
OsStatus_t
IoRead(
    _In_ int        Source,
    _In_ uintptr_t  Address,
    _In_ size_t     Width,
    _Out_ size_t   *Value)
{
    // Handle source-type
    if (Source == IO_SOURCE_MEMORY) {
        FATAL(FATAL_SCOPE_KERNEL, "IoRead(MEMORY)");
        if (Width == 1) {

        }
        else if (Width == 2) {

        }
        else if (Width == 4) {

        }
#if __BITS == 64
        else if (Width == 8) {

        }
#endif
        else {
            // Invalid width
            ERROR("(IoRead) Invalid width %u", Width);
            return OsError;
        }
        return OsSuccess;
    }
    else if (Source == IO_SOURCE_HARDWARE) {
        if (Width == 1) {
            *Value = ((size_t)inb((uint16_t)Address) & 0xFF);
        }
        else if (Width == 2) {
            *Value = ((size_t)inw((uint16_t)Address) & 0xFFFF);
        }
        else if (Width == 4) {
            *Value = ((size_t)inl((uint16_t)Address) & 0xFFFFFFFF);
        }
#if __BITS == 64
        else if (Width == 8) {
            size_t Temporary = inl((uint16_t)Address + 4);
            Temporary <<= 32;
            Temporary |= inl((uint16_t)Address);
            *Value = Temporary;
        }
#endif
        else {
            // Invalid width
            ERROR("(IoRead) Invalid width %u", Width);
            return OsError;
        }
        return OsSuccess;
    }

    // Invalid source
    ERROR("(IoRead) Invalid source");
    return OsError;
}

/* IoWrite 
 * Writes a value to the given data source. Accepted values in
 * width are 1, 2, 4 or 8. */
OsStatus_t
IoWrite(
    _In_ int        Source,
    _In_ uintptr_t  Address,
    _In_ size_t     Width,
    _In_ size_t     Value)
{
    // Handle source-type
    if (Source == IO_SOURCE_MEMORY) {
        FATAL(FATAL_SCOPE_KERNEL, "IoWrite(MEMORY)");
        if (Width == 1) {
            
        }
        else if (Width == 2) {
            
        }
        else if (Width == 4) {
            
        }
#if __BITS == 64
        else if (Width == 8) {

        }
#endif
        else {
            // Invalid width
            ERROR("(IoWrite) Invalid width %u", Width);
            return OsError;
        }
        return OsSuccess;
    }
    else if (Source == IO_SOURCE_HARDWARE) {
        if (Width == 1) {
            outb((uint16_t)Address, (uint8_t)(Value & 0xFF));
        }
        else if (Width == 2) {
            outw((uint16_t)Address, (uint16_t)(Value & 0xFFFF));
        }
        else if (Width == 4) {
            outl((uint16_t)Address, (uint32_t)(Value & 0xFFFFFFFF));
        }
#if __BITS == 64
        else if (Width == 8) {
            outl((uint16_t)Address + 4, HIDWORD(Value));
            outl((uint16_t)Address, LODWORD(Value));
        }
#endif
        else {
            // Invalid width
            ERROR("(IoWrite) Invalid width %u", Width);
            return OsError;
        }
        return OsSuccess;
    }

    // Invalid source
    ERROR("(IoWrite) Invalid source");
    return OsError;
}

/* PciRead
 * Reads a value from the given pci address. Accepted values in
 * width are 1, 2, 4 or 8. */
KERNELAPI
OsStatus_t
KERNELABI
PciRead(
    _In_ unsigned   Bus,
    _In_ unsigned   Slot,
    _In_ unsigned   Function,
    _In_ unsigned   Register,
    _In_ size_t     Width,
    _Out_ size_t   *Value)
{
    // Make sure width is of correct values
    if (Width == 1) {
        *Value = (size_t)PciRead8(Bus, Slot, Function, Register);
    }
    else if (Width == 2) {
        *Value = (size_t)PciRead16(Bus, Slot, Function, Register);
    }
    else if (Width == 4) {
        *Value = (size_t)PciRead32(Bus, Slot, Function, Register);
    }
    else {
        ERROR("(PciRead) Invalid width %u", Width);
        return OsError;
    }
    return OsSuccess;
}

/* PciWrite
 * Writes a value to the given pci address. Accepted values in
 * width are 1, 2, 4 or 8. */
KERNELAPI
OsStatus_t
KERNELABI
PciWrite(
    _In_ unsigned   Bus,
    _In_ unsigned   Slot,
    _In_ unsigned   Function,
    _In_ unsigned   Register,
    _In_ size_t     Width,
    _In_ size_t     Value)
{
    // Make sure width is of correct values
    if (Width == 1) {
        PciWrite8(Bus, Slot, Function, Register, (uint8_t)Value);
    }
    else if (Width == 2) {
        PciWrite16(Bus, Slot, Function, Register, (uint16_t)Value);
    }
    else if (Width == 4) {
        PciWrite32(Bus, Slot, Function, Register, (uint32_t)Value);
    }
    else {
        ERROR("(PciWrite) Invalid width %u", Width);
        return OsError;
    }
    return OsSuccess;
}
