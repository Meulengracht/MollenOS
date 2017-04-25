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

#ifndef _IO_INTEFACE_H_
#define _IO_INTEFACE_H_

/* Includes
 * - C-Library */
#include <os/osdefs.h>

/* The two different kinds of io-spaces that are
 * currently supported in MollenOS, to get which
 * kind of io-space check with IoSpace->Type */
#define IO_SPACE_INVALID		0x00
#define IO_SPACE_IO				0x01
#define IO_SPACE_MMIO			0x02

/* Represents an io-space in MollenOS, they represent
 * some kind of communication between hardware and software
 * by either port or mmio */
typedef struct _DeviceIoSpace {
	UUId_t						Id;
	int							Type;
	uintptr_t						PhysicalBase;
	uintptr_t						VirtualBase;
	size_t						Size;
} DeviceIoSpace_t;

/* Creates a new io-space and registers it with
 * the operation system, returns OsSuccess if it's 
 * a valid io-space */
MOSAPI OsStatus_t CreateIoSpace(DeviceIoSpace_t *IoSpace);

/* Tries to claim a given io-space, only one driver
 * can claim a single io-space at a time, to avoid
 * two drivers using the same device */
MOSAPI OsStatus_t AcquireIoSpace(DeviceIoSpace_t *IoSpace);

/* Tries to release a given io-space, only one driver
 * can claim a single io-space at a time, to avoid
 * two drivers using the same device */
MOSAPI OsStatus_t ReleaseIoSpace(DeviceIoSpace_t *IoSpace);

/* Destroys the io-space with the given id and removes
 * it from the io-manage in the operation system, it
 * can only be removed if its not already acquired */
MOSAPI OsStatus_t DestroyIoSpace(UUId_t IoSpace);

/* Read data from the given io-space at <offset> with 
 * the given <length>, the offset and length must be below 
 * the size of the io-space */
MOSAPI size_t ReadIoSpace(DeviceIoSpace_t *IoSpace, size_t Offset, size_t Length);

/* Write data from the given io-space at <offset> with 
 * the given <length>, the offset and length must be below 
 * the size of the io-space */
MOSAPI void WriteIoSpace(DeviceIoSpace_t *IoSpace, 
	size_t Offset, size_t Value, size_t Length);

#endif //!_IO_INTEFACE_H_
