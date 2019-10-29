/* MollenOS
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
 * MollenOS IO Space Interface
 * - Contains the shared kernel io space interface
 *   that all sub-layers / architectures must conform to
 */

#ifndef _MCORE_IOSPACE_H_
#define _MCORE_IOSPACE_H_

#include <os/osdefs.h>
#include <ds/collection.h>
#include <ddk/io.h>

// Operating system version of the DeviceIo
// Contains extra information as current owner of a space to increase
// security
typedef struct SystemDeviceIo {
    CollectionItem_t    Header;
    DeviceIo_t          Io;
    UUId_t              Owner;
    uintptr_t           MappedAddress;
} SystemDeviceIo_t;

/* RegisterSystemDeviceIo
 * Registers a new device memory io with the operating system. If this memory range
 * overlaps any existing io range, this request will be denied by the system. */
KERNELAPI OsStatus_t KERNELABI
RegisterSystemDeviceIo(
    _In_ DeviceIo_t*    IoSpace);

/* DestroySystemDeviceIo
 * Unregisters a device-io with the operating system, releasing all resources
 * associated and disabling the io range for use. */
KERNELAPI OsStatus_t KERNELABI
DestroySystemDeviceIo(
    _In_ DeviceIo_t*    IoSpace);

/* AcquireSystemDeviceIo
 * Tries to claim a given io-space, only one driver can claim a single io-space 
 * at a time, to avoid two drivers using the same device */
KERNELAPI OsStatus_t KERNELABI
AcquireSystemDeviceIo(
    _In_ DeviceIo_t*    IoSpace);

/* ReleaseSystemDeviceIo
 * Tries to release a given io-space, only one driver can claim a single io-space 
 * at a time, to avoid two drivers using the same device */
KERNELAPI OsStatus_t KERNELABI
ReleaseSystemDeviceIo(
    _In_ DeviceIo_t*    IoSpace);

/* AcquireKernelSystemDeviceIo
 * Creates a kernel mapped copy of the passed device-io. This can then be released
 * and cleaned up by the opposite call. */
KERNELAPI OsStatus_t KERNELABI
CreateKernelSystemDeviceIo(
    _In_  DeviceIo_t*   SourceIoSpace,
    _Out_ DeviceIo_t**  SystemIoSpace);

/* ReleaseKernelSystemDeviceIo 
 * Releases the kernel mapped copy of the passed device-io. */
KERNELAPI OsStatus_t KERNELABI
ReleaseKernelSystemDeviceIo(
    _In_ DeviceIo_t*    SystemIoSpace);

/* ValidateDeviceIoMemoryAddress (@interrupt_context)
 * Tries to validate the given virtual address by 
 * checking if any process has an active io-space
 * that involves that virtual address */
KERNELAPI uintptr_t KERNELABI
ValidateDeviceIoMemoryAddress(
    _In_ uintptr_t      Address);

#endif //!_MCORE_IOSPACE_H_
