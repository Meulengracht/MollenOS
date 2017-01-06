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
 * MollenOS MCore - Device I/O Definitions & Structures
 * - This header describes the base io-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _MCORE_DEVICE_IO_H_
#define _MCORE_DEVICE_IO_H_

/* Includes
 * - C-Library */
#include <os/osdefs.h>

/* The two different kinds of io-spaces that are
 * currently supported in MollenOS, to get which
 * kind of io-space check with IoSpace->Type */
#define IO_SPACE_INVALID		0x00
#define IO_SPACE_IO				0x01
#define IO_SPACE_MMIO			0x02

/* Structures */
typedef struct _DeviceIoSpace {
	IoSpaceId_t					Id;
	int							Type;
	Addr_t						PhysicalBase;
	Addr_t						VirtualBase;
	size_t						Size;
} DeviceIoSpace_t;

//_MOS_API CreateIoSpace(DeviceIoSpace_t *IoSpace);
//_MOS_API AcquireIoSpace(DeviceIoSpace_t *IoSpace)
//_MOS_API ReleaseIoSpace(DeviceIoSpace_t *IoSpace)
//_MOS_API DestroyIoSpace(DeviceIoSpace_t *IoSpace)

//_MOS_API ReadIoSpace(DeviceIoSpace_t *IoSpace, size_t Offset, size_t Length);
//_MOS_API WriteIoSpace(DeviceIoSpace_t *IoSpace, size_t Offset, size_t Value, size_t Length);

#endif //!_MCORE_DEVICE_IO_H_
