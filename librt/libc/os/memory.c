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

#include <os/mollenos.h>
#include <internal/_syscalls.h>

/* MemoryAllocate
 * Allocates a chunk of memory, controlled by the
 * requested size of memory. The returned memory will always
 * be rounded up to nearest page-size */
OsStatus_t
MemoryAllocate(
    _In_      void*         NearAddress,
	_In_      size_t        Length,
	_In_      Flags_t       Flags,
	_Out_     void**        MemoryPointer,
	_Out_Opt_ uintptr_t*    PhysicalPointer)
{
	// Variables
	OsStatus_t Result   = OsSuccess;
	uintptr_t Physical  = 0;
	uintptr_t Virtual   = 0;

	// Sanitize parameters
	if (Length == 0 || MemoryPointer == NULL) {
		return OsError;
	}
	Result = Syscall_MemoryAllocate(Length, Flags, &Virtual, &Physical);

	// Update memory-pointer
	*MemoryPointer = (void*)Virtual;
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
	return Syscall_MemoryFree(MemoryPointer, Length);
}

/* MemoryProtect
 * Changes the protection flags of a previous memory allocation
 * made by MemoryAllocate */
OsStatus_t
MemoryProtect(
    _In_  void*     MemoryPointer,
	_In_  size_t    Length,
    _In_  Flags_t   Flags,
    _Out_ Flags_t*  PreviousFlags)
{
    // Sanitize parameters
	if (Length == 0 || MemoryPointer == NULL) {
		return OsError;
	}
    return Syscall_MemoryProtect(MemoryPointer, Length, Flags, PreviousFlags);
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
	return Syscall_MemoryQuery(Descriptor);
}
