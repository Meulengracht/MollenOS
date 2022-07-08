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
 * MollenOS X86 IO Driver
 */
 
#define __MODULE "IOLL"

#include <arch/io.h>
#include <ddk/barrier.h>
#include <debug.h>
#include <arch/x86/arch.h>
#include <arch/x86/pci.h>

extern uint8_t  inb(uint16_t port);
extern uint16_t inw(uint16_t port);
extern uint32_t inl(uint16_t port);
extern void     outb(uint16_t port, uint8_t data);
extern void     outw(uint16_t port, uint16_t data);
extern void     outl(uint16_t port, uint32_t data);

size_t
PciCalculateOffset(
    _In_ int       IsExtended,
	_In_ unsigned int Bus,
    _In_ unsigned int Device,
    _In_ unsigned int Function,
    _In_ size_t    Register)
{
	if (IsExtended) {
		return (size_t)((Bus << 20) | (Device << 15) | (Function << 12) | Register);
	}
	else {
		return (size_t)(0x80000000 | (Bus << 16) | (Device << 11) 
			| (Function << 8) | (Register & 0xFC));
	}
}

uint32_t
PciRead32(
    _In_ unsigned int Bus,
    _In_ unsigned int Device,
    _In_ unsigned int Function,
    _In_ size_t    Register)
{
	outl(X86_PCI_SELECT, PciCalculateOffset(0, Bus, Device, Function, Register));
	return inl(X86_PCI_DATA);
}

uint16_t
PciRead16(
    _In_ unsigned int Bus,
    _In_ unsigned int Device,
    _In_ unsigned int Function,
    _In_ size_t    Register)
{
	outl(X86_PCI_SELECT, PciCalculateOffset(0, Bus, Device, Function, Register));
	return inw(X86_PCI_DATA + (Register & 0x02));
}

uint8_t
PciRead8(
    _In_ unsigned int Bus,
    _In_ unsigned int Device,
    _In_ unsigned int Function,
    _In_ size_t    Register)
{
	outl(X86_PCI_SELECT, PciCalculateOffset(0, Bus, Device, Function, Register));
	return inb(X86_PCI_DATA + (Register & 0x03));
}

void
PciWrite32(
    _In_ unsigned int Bus,
    _In_ unsigned int Device,
	_In_ unsigned int Function,
    _In_ size_t    Register,
    _In_ uint32_t  Value)
{
	outl(X86_PCI_SELECT, PciCalculateOffset(0, Bus, Device, Function, Register));
	outl(X86_PCI_DATA, Value);
}

void
PciWrite16(
    _In_ unsigned int Bus,
    _In_ unsigned int Device, 
	_In_ unsigned int Function,
    _In_ size_t    Register,
    _In_ uint16_t  Value)
{
	outl(X86_PCI_SELECT, PciCalculateOffset(0, Bus, Device, Function, Register));
	outw(X86_PCI_DATA + (Register & 0x02), Value);
}

void PciWrite8(
    _In_ unsigned int Bus,
    _In_ unsigned int Device, 
	_In_ unsigned int Function,
    _In_ size_t    Register,
    _In_ uint8_t   Value)
{
	outl(X86_PCI_SELECT, PciCalculateOffset(0, Bus, Device, Function, Register));
	outw(X86_PCI_DATA + (Register & 0x03), Value);
}

oserr_t
ReadDirectIo(
    _In_  DeviceIoType_t Type,
    _In_  uintptr_t      Address,
    _In_  size_t         Width,
    _Out_ size_t*        Value)
{
    switch (Type) {
        case DeviceIoPortBased: {
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
                uint64_t Temporary = inl((uint16_t)Address + 4);
                Temporary <<= 32;
                Temporary |= inl((uint16_t)Address);
                *Value = Temporary;
            }
#endif
            else {
                ERROR(" > invalid port width %" PRIuIN " for reading", Width);
                return OsError;
            }
        } break;

        default: {
            FATAL(FATAL_SCOPE_KERNEL, " > invalid direct io read type %" PRIuIN "", Type);
        } break;
    }
    return OsOK;
}

oserr_t
WriteDirectIo(
    _In_ DeviceIoType_t Type,
    _In_ uintptr_t      Address,
    _In_ size_t         Width,
    _In_ size_t         Value)
{
    switch (Type) {
        case DeviceIoPortBased: {
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
                ERROR(" > invalid port width %" PRIuIN " for writing", Width);
                return OsError;
            }
        } break;

        default: {
            FATAL(FATAL_SCOPE_KERNEL, " > invalid direct io write type %" PRIuIN "", Type);
        } break;
    }
    return OsOK;
}

oserr_t
ReadDirectPci(
    _In_  unsigned Bus,
    _In_  unsigned Slot,
    _In_  unsigned Function,
    _In_  unsigned Register,
    _In_  size_t   Width,
    _Out_ size_t*  Value)
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
        ERROR("(PciRead) Invalid width %" PRIuIN "", Width);
        return OsError;
    }
    return OsOK;
}

oserr_t
WriteDirectPci(
    _In_ unsigned Bus,
    _In_ unsigned Slot,
    _In_ unsigned Function,
    _In_ unsigned Register,
    _In_ size_t   Width,
    _In_ size_t   Value)
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
        ERROR("(PciWrite) Invalid width %" PRIuIN "", Width);
        return OsError;
    }
    return OsOK;
}
