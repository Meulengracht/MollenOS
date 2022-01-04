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
 * I/O Definitions & Structures
 * - This header describes the base io-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <internal/_syscalls.h>
#include <ddk/io.h>
#include <assert.h>

ASMDECL(uint8_t,  __readbyte(uint16_t Port));
ASMDECL(void,     __writebyte(uint16_t Port, uint8_t Value));
ASMDECL(uint16_t, __readword(uint16_t Port));
ASMDECL(void,     __writeword(uint16_t Port, uint16_t Value));
ASMDECL(uint32_t, __readlong(uint16_t Port));
ASMDECL(void,     __writelong(uint16_t Port, uint32_t Value));

/* CreateDeviceMemoryIo
 * Registers a new device memory io with the operating system. If this memory range
 * overlaps any existing io range, this request will be denied by the system. */
OsStatus_t
CreateDeviceMemoryIo(
    _In_ DeviceIo_t*    IoSpace,
    _In_ uintptr_t      PhysicalBase,
    _In_ size_t         Length)
{
    assert(IoSpace != NULL);
    IoSpace->Type                       = DeviceIoMemoryBased;
    IoSpace->Access.Memory.PhysicalBase = PhysicalBase;
    IoSpace->Access.Memory.VirtualBase  = 0;
    IoSpace->Access.Memory.Length       = Length;
    return Syscall_IoSpaceRegister(IoSpace);
}

/* CreateDevicePortIo
 * Registers a new device port io with the operating system. If this port io range
 * overlaps any existing range, this request will be denied by the system. */
OsStatus_t
CreateDevicePortIo(
    _In_ DeviceIo_t*    IoSpace,
    _In_ uint16_t       Port,
    _In_ size_t         Length)
{
    assert(IoSpace != NULL);
    IoSpace->Type               = DeviceIoPortBased;
    IoSpace->Access.Port.Base   = Port;
    IoSpace->Access.Port.Length = Length;
    return Syscall_IoSpaceRegister(IoSpace);
}

/* CreateDevicePinIo
 * Registers a new device port/pin io with the operating system. If this port/pin
 * overlaps any existing port/pin, this request will be denied by the system. */
OsStatus_t
CreateDevicePinIo(
    _In_ DeviceIo_t*    IoSpace,
    _In_ uint16_t       Port,
    _In_ uint8_t        Pin)
{
    assert(IoSpace != NULL);
    IoSpace->Type               = DeviceIoPinBased;
    IoSpace->Access.Pin.Port    = Port;
    IoSpace->Access.Pin.Pin     = Pin;
    return Syscall_IoSpaceRegister(IoSpace);
}

/* DestroyDeviceIo
 * Unregisters a device-io with the operating system, releasing all resources
 * associated and disabling the io range for use. */
OsStatus_t
DestroyDeviceIo(
    _In_ DeviceIo_t*    IoSpace)
{
    assert(IoSpace != NULL);
    return Syscall_IoSpaceDestroy(IoSpace);
}

/* AcquireDeviceIo
 * Tries to claim a given io-space, only one driver can claim a single io-space 
 * at a time, to avoid two drivers using the same device */
OsStatus_t
AcquireDeviceIo(
    _Out_ DeviceIo_t*   IoSpace)
{
    assert(IoSpace != NULL);
    return Syscall_IoSpaceAcquire(IoSpace);
}

/* ReleaseDeviceIo
 * Tries to release a given io-space, only one driver can claim a single io-space 
 * at a time, to avoid two drivers using the same device */
OsStatus_t
ReleaseDeviceIo(
    _In_ DeviceIo_t*    IoSpace)
{
    assert(IoSpace != NULL);
    return Syscall_IoSpaceRelease(IoSpace);
}

/* ReadDeviceIo
 * Read data from the given io-space at <offset> with the given <length>, 
 * the offset and length must be below the size of the io-space */
size_t
ReadDeviceIo(
    _In_ DeviceIo_t*    IoSpace,
    _In_ size_t         Offset,
    _In_ size_t         Length)
{
    size_t Result = 0;
    assert(IoSpace != NULL);

    switch (IoSpace->Type) {
        case DeviceIoMemoryBased: {
            uintptr_t Address = IoSpace->Access.Memory.VirtualBase + Offset;
            assert((Offset + Length) <= IoSpace->Access.Memory.Length);
            assert(IoSpace->Access.Memory.VirtualBase != 0);
            switch (Length) {
                case 1:
                    Result = *(uint8_t*)Address;
                    break;
                case 2:
                    Result = *(uint16_t*)Address;
                    break;
                case 4:
                    Result = *(uint32_t*)Address;
                    break;
    #if defined(__amd64__)
                case 8:
                    Result = *(uint64_t*)Address;
                    break;
    #endif
                default:
                    break;
            }
        } break;

        case DeviceIoPortBased: {
            uint16_t Port = LOWORD(IoSpace->Access.Port.Base) + LOWORD(Offset);
            switch (Length) {
                case 1:
                    Result = __readbyte(Port);
                    break;
                case 2:
                    Result = __readword(Port);
                    break;
                case 4:
                    Result = __readlong(Port);
                    break;
                default:
                    break;
            }
        } break;

        default:
            break;
    }
    return Result;
}

/* WriteDeviceIo
 * Write data from the given io-space at <offset> with the given <length>, 
 * the offset and length must be below the size of the io-space */
OsStatus_t
WriteDeviceIo(
    _In_ DeviceIo_t*    IoSpace,
    _In_ size_t         Offset,
    _In_ size_t         Value,
    _In_ size_t         Length)
{
    OsStatus_t Status = OsSuccess;
    assert(IoSpace != NULL);

    switch (IoSpace->Type) {
        case DeviceIoMemoryBased: {
            uintptr_t Address = IoSpace->Access.Memory.VirtualBase + Offset;
            assert((Offset + Length) <= IoSpace->Access.Memory.Length);
            assert(IoSpace->Access.Memory.VirtualBase != 0);
            switch (Length) {
                case 1:
                    *(uint8_t*)Address = (uint8_t)(Value & 0xFF);
                    break;
                case 2:
                    *(uint16_t*)Address = (uint16_t)(Value & 0xFFFF);
                    break;
                case 4:
                    *(uint32_t*)Address = (uint32_t)(Value & 0xFFFFFFFF);
                    break;
    #if defined(__amd64__)
                case 8:
                    *(uint64_t*)Address = (uint64_t)(Value & 0xFFFFFFFFFFFFFFFF);
                    break;
    #endif
                default:
                    Status = OsError;
                    break;
            }
        } break;
        
        case DeviceIoPortBased: {
            uint16_t Port = LOWORD(IoSpace->Access.Port.Base) + LOWORD(Offset);
            switch (Length) {
                case 1:
                    __writebyte(Port, (uint8_t)(Value & 0xFF));
                    break;
                case 2:
                    __writeword(Port, (uint16_t)(Value & 0xFFFF));
                    break;
                case 4:
                    __writelong(Port, (uint32_t)(Value & 0xFFFFFFFF));
                    break;
                default:
                    Status = OsError;
                    break;
            }
        } break;

        default:
            Status = OsError;
            break;
    }
    return Status;
}
