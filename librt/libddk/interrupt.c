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
    
    // Store pci identification information
    interrupt->IsPci = device->IsPci;
    interrupt->Segment = device->Segment;
    interrupt->Bus = device->Bus;
    interrupt->Slot = device->Slot;
    interrupt->Function = device->Function;
    interrupt->DeviceId = device->Base.Id;
    
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

uuid_t
RegisterInterruptSource(
    _In_ DeviceInterrupt_t* interrupt,
    _In_ unsigned int       flags)
{
	return Syscall_InterruptAdd(interrupt, flags);
}

oserr_t
DeviceInterruptSetRegister(
    _In_  DeviceInterrupt_t*    interrupts,
    _In_  uint32_t              count,
    _In_  unsigned int          flags,
    _Out_ DeviceInterruptSet_t* setOut)
{
    return Syscall_InterruptSetAdd(interrupts, count, flags, setOut);
}

oserr_t
DeviceInterruptAllocate(
    _In_  DeviceInterrupt_t*    vectors,
    _In_  uint32_t              min,
    _In_  uint32_t              optimal,
    _In_  uint32_t              strategy,
    _Out_ DeviceInterruptSet_t* setOut,
    _Out_ uint32_t*             countOut,
    _Out_ uint32_t*             strategyOut)
{
    uint32_t count;
    oserr_t  oserr = OS_ENOTSUPPORTED;

    if (!vectors || !setOut || !countOut || !strategyOut || min == 0 ||
        min > optimal || optimal > INTERRUPT_MAXVECTORS || strategy == 0 ||
        (strategy & ~INTERRUPT_STRATEGY_ANY) != 0) {
        return OS_EINVALPARAMS;
    }

    *setOut      = UUID_INVALID;
    *countOut    = 0;
    *strategyOut = 0;

    if (strategy & INTERRUPT_STRATEGY_MSIX) {
        for (count = optimal; count >= min; count--) {
            oserr = DeviceInterruptSetRegister(
                vectors,
                count,
                INTERRUPT_MSI | INTERRUPT_EXCLUSIVE,
                setOut
            );
            if (oserr == OS_EOK) {
                *countOut    = count;
                *strategyOut = INTERRUPT_STRATEGY_MSIX;
                return OS_EOK;
            }
            if (count == min) {
                break;
            }
        }
    }

    if (min == 1 && (strategy & INTERRUPT_STRATEGY_MSI)) {
        oserr = DeviceInterruptSetRegister(
            vectors,
            1,
            INTERRUPT_MSI | INTERRUPT_EXCLUSIVE,
            setOut
        );
        if (oserr == OS_EOK) {
            *countOut    = 1;
            *strategyOut = INTERRUPT_STRATEGY_MSI;
            return OS_EOK;
        }
    }

    if (min == 1 && (strategy & INTERRUPT_STRATEGY_INTx)) {
        oserr = DeviceInterruptSetRegister(vectors, 1, 0, setOut);
        if (oserr == OS_EOK) {
            *countOut    = 1;
            *strategyOut = INTERRUPT_STRATEGY_INTx;
            return OS_EOK;
        }
    }
    return oserr;
}

oserr_t
DeviceInterruptSetDestroy(
    _In_ DeviceInterruptSet_t interruptSet)
{
    return Syscall_InterruptSetRemove(interruptSet);
}

oserr_t
DeviceInterruptQuiesceRegister(
    _In_ int eventDescriptor)
{
    if (eventDescriptor < 0) {
        return OS_EINVALPARAMS;
    }
    return Syscall_InterruptQuiesceRegister(GetNativeHandle(eventDescriptor));
}

oserr_t
DeviceInterruptQuiesceNext(
    _Out_ DeviceInterruptQuiesceRequest_t* requestOut)
{
    return Syscall_InterruptQuiesceNext(requestOut);
}

oserr_t
DeviceInterruptQuiesceComplete(
    _In_ uuid_t token)
{
    return Syscall_InterruptQuiesceComplete(token);
}

uuid_t
DeviceInterruptMsiControllerRegister(
    _In_ const DeviceMsiControllerDescription_t* description)
{
    if (description == NULL) {
        return UUID_INVALID;
    }
    return Syscall_InterruptMsiControllerRegister(description);
}

oserr_t
UnregisterInterruptSource(
        _In_ uuid_t interruptHandle)
{
	return Syscall_InterruptRemove(interruptHandle);
}
