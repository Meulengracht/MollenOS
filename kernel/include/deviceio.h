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
 * IO Space Interface
 * - Contains the shared kernel io space interface
 *   that all sub-layers / architectures must conform to
 */

#ifndef __VALI_IOSPACE_H__
#define __VALI_IOSPACE_H__

#include <ddk/io.h>
#include <os/osdefs.h>

/* RegisterSystemDeviceIo
 * Registers a new device memory io with the operating system. If this memory range
 * overlaps any existing io range, this request will be denied by the system. */
KERNELAPI OsStatus_t KERNELABI
RegisterSystemDeviceIo(
    _In_ DeviceIo_t* IoSpace);

/* AcquireSystemDeviceIo
 * Tries to claim a given io-space, only one driver can claim a single io-space 
 * at a time, to avoid two drivers using the same device */
KERNELAPI OsStatus_t KERNELABI
AcquireSystemDeviceIo(
    _In_ DeviceIo_t* IoSpace);

/* ReleaseSystemDeviceIo
 * Tries to release a given io-space, only one driver can claim a single io-space 
 * at a time, to avoid two drivers using the same device */
KERNELAPI OsStatus_t KERNELABI
ReleaseSystemDeviceIo(
    _In_ DeviceIo_t* IoSpace);

/* AcquireKernelSystemDeviceIo
 * Creates a kernel mapped copy of the passed device-io. This can then be released
 * and cleaned up by the opposite call. */
KERNELAPI OsStatus_t KERNELABI
CreateKernelSystemDeviceIo(
    _In_  DeviceIo_t*  SourceIoSpace,
    _Out_ DeviceIo_t** SystemIoSpace);

/* ReleaseKernelSystemDeviceIo 
 * Releases the kernel mapped copy of the passed device-io. */
KERNELAPI OsStatus_t KERNELABI
ReleaseKernelSystemDeviceIo(
    _In_ DeviceIo_t* SystemIoSpace);

/* ValidateDeviceIoMemoryAddress (@interrupt_context)
 * Tries to validate the given virtual address by 
 * checking if any process has an active io-space
 * that involves that virtual address */
KERNELAPI uintptr_t KERNELABI
ValidateDeviceIoMemoryAddress(
    _In_ uintptr_t Address);

#endif //!__VALI_IOSPACE_H__
