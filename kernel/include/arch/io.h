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
 * MollenOS Io Interface
 * - Contains a glue layer to access hardware-io functionality
 *   that all sub-layers / architectures must conform to
 */
#ifndef __SYSTEM_IO_INTEFACE_H__
#define __SYSTEM_IO_INTEFACE_H__

#include <os/osdefs.h>
#include <memoryspace.h>
#include <deviceio.h>

/* ReadDirectIo 
 * Reads a value from the given raw io source. Accepted values in width are 1, 2, 4 or 8. */
KERNELAPI OsStatus_t KERNELABI
ReadDirectIo(
    _In_  DeviceIoType_t Type,
    _In_  uintptr_t      Address,
    _In_  size_t         Width,
    _Out_ size_t*        Value);

/* WriteDirectIo 
 * Writes a value to the given raw io source. Accepted values in width are 1, 2, 4 or 8. */
KERNELAPI OsStatus_t KERNELABI
WriteDirectIo(
    _In_ DeviceIoType_t Type,
    _In_ uintptr_t      Address,
    _In_ size_t         Width,
    _In_ size_t         Value);

/* ReadDirectPci
 * Reads a value from the given pci address. Accepted values in width are 1, 2, 4 or 8. */
KERNELAPI OsStatus_t KERNELABI
ReadDirectPci(
    _In_  unsigned Bus,
    _In_  unsigned Slot,
    _In_  unsigned Function,
    _In_  unsigned Register,
    _In_  size_t   Width,
    _Out_ size_t*  Value);

/* WriteDirectPci
 * Writes a value to the given pci address. Accepted values in width are 1, 2, 4 or 8. */
KERNELAPI OsStatus_t KERNELABI
WriteDirectPci(
    _In_ unsigned Bus,
    _In_ unsigned Slot,
    _In_ unsigned Function,
    _In_ unsigned Register,
    _In_ size_t   Width,
    _In_ size_t   Value);

/* SetDirectIoAccess
 * Set's the io status of the given memory space. */
KERNELAPI OsStatus_t KERNELABI
SetDirectIoAccess(
        _In_ UUId_t               coreId,
        _In_ MemorySpace_t* memorySpace,
        _In_ uint16_t             port,
        _In_ int                  enable);

#endif //!__SYSTEM_IO_INTEFACE_H__
