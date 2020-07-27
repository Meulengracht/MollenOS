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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Interrupt Support Definitions & Structures
 * - This header describes the base interrupt-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <internal/_syscalls.h>
#include <ddk/busdevice.h>
#include <ddk/interrupt.h>
#include <string.h>
#include <internal/_utils.h>

void
DeviceInterruptInitialize(
    _In_ DeviceInterrupt_t* Interrupt,
    _In_ BusDevice_t*       Device)
{
    memset(Interrupt, 0, sizeof(DeviceInterrupt_t));
    
    Interrupt->Line        = Device->InterruptLine;
    Interrupt->Pin         = Device->InterruptPin;
    Interrupt->AcpiConform = Device->InterruptAcpiConform;
    Interrupt->Vectors[0]  = INTERRUPT_NONE;
}

void
RegisterFastInterruptHandler(
    _In_ DeviceInterrupt_t* Interrupt,
    _In_ InterruptHandler_t Handler)
{
    Interrupt->ResourceTable.Handler = Handler;
}

void
RegisterFastInterruptIoResource(
    _In_ DeviceInterrupt_t* Interrupt,
    _In_ DeviceIo_t*        IoSpace)
{
    for (int i = 0; i < INTERRUPT_MAX_IO_RESOURCES; i++) {
        if (Interrupt->ResourceTable.IoResources[i] == NULL) {
            Interrupt->ResourceTable.IoResources[i] = IoSpace;
            break;
        }
    }
}

void
RegisterFastInterruptMemoryResource(
    _In_ DeviceInterrupt_t* Interrupt,
    _In_ uintptr_t          Address,
    _In_ size_t             Length,
    _In_ unsigned int       Flags)
{
    for (int i = 0; i < INTERRUPT_MAX_MEMORY_RESOURCES; i++) {
        if (Interrupt->ResourceTable.MemoryResources[i].Address == 0) {
            Interrupt->ResourceTable.MemoryResources[i].Address = Address;
            Interrupt->ResourceTable.MemoryResources[i].Length  = Length;
            Interrupt->ResourceTable.MemoryResources[i].Flags   = Flags;
            break;
        }
    }
}

void
RegisterInterruptDescriptor(
    _In_ DeviceInterrupt_t* interrupt,
    _In_ int                descriptor)
{
    if (!interrupt) {
        return;
    }

    interrupt->ResourceTable.ResourceHandle = GetNativeHandle(descriptor);
}

UUId_t
RegisterInterruptSource(
    _In_ DeviceInterrupt_t* Interrupt,
    _In_ unsigned int       Flags)
{
	// Sanitize input
	if (Interrupt == NULL) {
		return UUID_INVALID;
	}
	return Syscall_InterruptAdd(Interrupt, Flags);
}

OsStatus_t
UnregisterInterruptSource(
    _In_ UUId_t Source)
{
	// Sanitize input
	if (Source == UUID_INVALID) {
		return OsError;
	}
	return Syscall_InterruptRemove(Source);
}
