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

/* MemoryAllocate
 * Allocates a chunk of memory, controlled by the
 * requested size of memory. The returned memory will always
 * be rounded up to nearest page-size */
OsStatus_t
MemoryAllocate(
	_In_ size_t Length,
	_In_ Flags_t Flags,
	_Out_ void **MemoryPointer,
	_Out_Opt_ uintptr_t *PhysicalPointer)
{
	// Variables
	uintptr_t Physical = 0;
	uintptr_t Virtual = 0;
	OsStatus_t Result;

	// Sanitize parameters
	if (Length == 0 || MemoryPointer == NULL) {
		return OsError;
	}

	// Redirect call to OS
	Result = (OsStatus_t)Syscall4(SYSCALL_MEMALLOC,
		SYSCALL_PARAM(Length), SYSCALL_PARAM(Flags),
		SYSCALL_PARAM(&Virtual), SYSCALL_PARAM(&Physical));

	// Update memory-pointer
	*MemoryPointer = (void*)Virtual;

	// Update the physical out in case its given
	if (PhysicalPointer != NULL) {
		*PhysicalPointer = Physical;
	}
	return Result;
}

/* MemoryFree
 * Frees previously allocated memory and releases
 * the system resources associated. */
OsStatus_t
MemoryFree(
	_In_ void *MemoryPointer,
	_In_ size_t Length)
{
	// Sanitize parameters
	if (Length == 0 || MemoryPointer == NULL) {
		return OsError;
	}

	// Redirect call to OS - no post ops here
	return (OsStatus_t)Syscall2(SYSCALL_MEMFREE,
		SYSCALL_PARAM(MemoryPointer), SYSCALL_PARAM(Length));
}

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
	// Sanitize parameters
	if (Buffer == NULL || Size == 0) {
		return NULL;
	}

	// Redirect call to OS - no post ops here
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
	// Sanitize parameters
	if (MemoryHandle == NULL || Size == 0) {
		return OsError;
	}

	// Redirect call to OS - no post ops here
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
	// Sanitize parameters
	if (Descriptor == NULL) {
		return OsError;
	}

	// Redirect call to OS - no post ops here
	return (OsStatus_t)Syscall1(
		SYSCALL_MEMQUERY, SYSCALL_PARAM(Descriptor));
}
