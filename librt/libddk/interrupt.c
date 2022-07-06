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
    _In_ DeviceInterrupt_t* interrupt,
    _In_ BusDevice_t*       device)
{
    if (!interrupt ||  !device) {
        return;
    }

    memset(interrupt, 0, sizeof(DeviceInterrupt_t));

    interrupt->Line        = device->InterruptLine;
    interrupt->Pin         = device->InterruptPin;
    interrupt->AcpiConform = device->InterruptAcpiConform;
    interrupt->Vectors[0] = INTERRUPT_NONE;
}

void
RegisterFastInterruptHandler(
    _In_ DeviceInterrupt_t* interrupt,
    _In_ InterruptHandler_t handler)
{
    if (!interrupt) {
        return;
    }

    interrupt->ResourceTable.Handler = handler;
}

void
RegisterFastInterruptIoResource(
    _In_ DeviceInterrupt_t* interrupt,
    _In_ DeviceIo_t*        ioSpace)
{
    if (!interrupt) {
        return;
    }

    for (int i = 0; i < INTERRUPT_MAX_IO_RESOURCES; i++) {
        if (!interrupt->ResourceTable.IoResources[i]) {
            interrupt->ResourceTable.IoResources[i] = ioSpace;
            break;
        }
    }
}

void
RegisterFastInterruptMemoryResource(
    _In_ DeviceInterrupt_t* interrupt,
    _In_ uintptr_t          address,
    _In_ size_t             length,
    _In_ unsigned int       flags)
{
    if (!interrupt) {
        return;
    }

    for (int i = 0; i < INTERRUPT_MAX_MEMORY_RESOURCES; i++) {
        if (interrupt->ResourceTable.MemoryResources[i].Address == 0) {
            interrupt->ResourceTable.MemoryResources[i].Address = address;
            interrupt->ResourceTable.MemoryResources[i].Length  = length;
            interrupt->ResourceTable.MemoryResources[i].Flags   = flags;
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

    interrupt->ResourceTable.HandleResource = GetNativeHandle(descriptor);
}

UUId_t
RegisterInterruptSource(
    _In_ DeviceInterrupt_t* interrupt,
    _In_ unsigned int       flags)
{
	return Syscall_InterruptAdd(interrupt, flags);
}

oscode_t
UnregisterInterruptSource(
    _In_ UUId_t interruptHandle)
{
	return Syscall_InterruptRemove(interruptHandle);
}
