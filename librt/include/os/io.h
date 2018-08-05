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
 * MollenOS MCore - Device I/O Definitions & Structures
 * - This header describes the base io-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __DEVICEIO_INTERFACE_H__
#define __DEVICEIO_INTERFACE_H__

#include <os/osdefs.h>

typedef enum _DeviceIoType {
    DeviceIoInvalid     = 0,
    DeviceIoMemoryBased,            // Usually device memory range
    DeviceIoPortBased,              // Usually a port range
    DeviceIoPinBased                // Usually a port/pin combination
} DeviceIoType_t;

// Represents a device io communcation space
// that can be used by a driver to communcate with its physical device.
typedef struct _DeviceIo {
    UUId_t              Id;
    DeviceIoType_t      Type;
    union {
        struct {
            uintptr_t   PhysicalBase;
            uintptr_t   VirtualBase;
            size_t      Length;
        } Memory;
        struct {
            uint16_t    Base;
            size_t      Length;
        } Port;
        struct {
            uint16_t    Port;
            uint8_t     Pin;
        } Pin;
    } Access;
} DeviceIo_t;

_CODE_BEGIN
/* CreateDeviceMemoryIo
 * Registers a new device memory io with the operating system. If this memory range
 * overlaps any existing io range, this request will be denied by the system. */
CRTDECL(OsStatus_t,
CreateDeviceMemoryIo(
    _In_ DeviceIo_t*    IoSpace,
    _In_ uintptr_t      PhysicalBase,
    _In_ size_t         Length));

/* CreateDevicePortIo
 * Registers a new device port io with the operating system. If this port io range
 * overlaps any existing range, this request will be denied by the system. */
CRTDECL(OsStatus_t,
CreateDevicePortIo(
    _In_ DeviceIo_t*    IoSpace,
    _In_ uint16_t       Port,
    _In_ size_t         Length));

/* CreateDevicePinIo
 * Registers a new device port/pin io with the operating system. If this port/pin
 * overlaps any existing port/pin, this request will be denied by the system. */
CRTDECL(OsStatus_t,
CreateDevicePinIo(
    _In_ DeviceIo_t*    IoSpace,
    _In_ uint16_t       Port,
    _In_ uint8_t        Pin));

/* DestroyDeviceIo
 * Unregisters a device-io with the operating system, releasing all resources
 * associated and disabling the io range for use. */
CRTDECL(OsStatus_t,
DestroyDeviceIo(
    _In_ DeviceIo_t*    IoSpace));

/* AcquireDeviceIo
 * Tries to claim a given io-space, only one driver can claim a single io-space 
 * at a time, to avoid two drivers using the same device */
CRTDECL(OsStatus_t,
AcquireDeviceIo(
    _In_ DeviceIo_t*    IoSpace));

/* ReleaseDeviceIo
 * Tries to release a given io-space, only one driver can claim a single io-space 
 * at a time, to avoid two drivers using the same device */
CRTDECL(OsStatus_t,
ReleaseDeviceIo(
    _In_ DeviceIo_t*    IoSpace));

/* ReadDeviceIo
 * Read data from the given io-space at <offset> with the given <length>, 
 * the offset and length must be below the size of the io-space */
CRTDECL(size_t,
ReadDeviceIo(
    _In_ DeviceIo_t*    IoSpace,
    _In_ size_t         Offset,
    _In_ size_t         Length));

/* WriteDeviceIo
 * Write data from the given io-space at <offset> with the given <length>, 
 * the offset and length must be below the size of the io-space */
CRTDECL(OsStatus_t,
WriteDeviceIo(
    _In_ DeviceIo_t*    IoSpace,
    _In_ size_t         Offset,
    _In_ size_t         Value,
    _In_ size_t         Length));
_CODE_END

#endif //!__DEVICEIO_INTERFACE_H__
