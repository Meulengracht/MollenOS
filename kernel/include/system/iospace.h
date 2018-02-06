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
 * MollenOS IO Space Interface
 * - Contains the shared kernel io space interface
 *   that all sub-layers / architectures must conform to
 */

#ifndef _MCORE_IOSPACE_H_
#define _MCORE_IOSPACE_H_

/* Includes 
 * - Library */
#include <os/osdefs.h>
#include <os/spinlock.h>
#include <os/io.h>

/* IoSpaceInitialize
 * Initialize the Io Space manager so we 
 * can register io-spaces from drivers and the
 * bus code */
KERNELAPI
void
KERNELABI
IoSpaceInitialize(void);

/* IoSpaceRegister
 * Registers an io-space with the io space manager 
 * and assigns the io-space a unique id for later
 * identification */
KERNELAPI
OsStatus_t
KERNELABI
IoSpaceRegister(
	_In_ DeviceIoSpace_t *IoSpace);

/* IoSpaceAcquire
 * Acquires the given memory space by mapping it in
 * the current drivers memory space if needed, and sets
 * a lock on the io-space */
KERNELAPI
OsStatus_t
KERNELABI
IoSpaceAcquire(
	_In_ DeviceIoSpace_t *IoSpace);

/* IoSpaceRelease
 * Releases the given memory space by unmapping it from
 * the current drivers memory space if needed, and releases
 * the lock on the io-space */
KERNELAPI
OsStatus_t
KERNELABI
IoSpaceRelease(
	_In_ DeviceIoSpace_t *IoSpace);

/* IoSpaceDestroy
 * Destroys the given io-space by its id, the id
 * has the be valid, and the target io-space HAS to 
 * un-acquired by any process, otherwise its not possible */
KERNELAPI
OsStatus_t
KERNELABI
IoSpaceDestroy(
	_In_ UUId_t IoSpace);

/* IoSpaceValidate
 * Tries to validate the given virtual address by 
 * checking if any process has an active io-space
 * that involves that virtual address */
KERNELAPI
uintptr_t
KERNELABI
IoSpaceValidate(
	_In_ uintptr_t Address);

#endif //!_MCORE_IOSPACE_H_
