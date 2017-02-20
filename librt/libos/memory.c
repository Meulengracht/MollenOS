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
 * MollenOS MCore - Memory Definitions & Structures
 * - This header describes the memory-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

/* Includes
 * - System */
#include <os/mollenos.h>
#include <os/syscall.h>

/* Includes
 * - Library */
#include <stddef.h>

/* MemoryShare
 * This shares a piece of memory with the 
 * target process. The function returns NULL
 * on failure to share the piece of memory
 * otherwise it returns the new buffer handle
 * that can be accessed by the other process */ 
void *
MemoryShare(
	_In_ UUId_t Process,
	_In_ void *Buffer,
	_In_ size_t Size)
{
	/* Sanitize parameters */
	if (Buffer == NULL || Size == 0) {
		return NULL;
	}

	/* Redirect to os-sublayer */
	return (void*)Syscall3(SYSCALL_MEMSHARE, SYSCALL_PARAM(Process),
		SYSCALL_PARAM(Buffer), SYSCALL_PARAM(Size));
}

/* MemoryUnshare
 * This takes a previous shared memory handle 
 * and unshares it again from the target process */ 
OsStatus_t 
MemoryUnshare(
	_In_ UUId_t Process,
	_In_ void *MemoryHandle,
	_In_ size_t Size)
{
	/* Sanitize parameters */
	if (MemoryHandle == NULL || Size == 0) {
		return OsError;
	}

	/* Redirect to os-sublayer */
	return (OsStatus_t)Syscall3(SYSCALL_MEMUNSHARE, SYSCALL_PARAM(Process),
		SYSCALL_PARAM(MemoryHandle), SYSCALL_PARAM(Size));
}

/* MemoryQuery
 * Queries the underlying system for memory information 
 * like memory used and the page-size */
OsStatus_t
MemoryQuery(
	_Out_ MemoryDescriptor_t *Descriptor)
{
	/* Sanitize parameters */
	if (Descriptor == NULL) {
		return OsError;
	}

	/* Redirect to os-sublayer */
	return (OsStatus_t)Syscall1(
		SYSCALL_MEMQUERY, SYSCALL_PARAM(Descriptor));
}
