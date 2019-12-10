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
 * Memory Definitions & Structures
 * - This header describes the memory-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <os/mollenos.h>
#include <internal/_syscalls.h>

OsStatus_t
MemoryAllocate(
    _In_      void*   Hint,
    _In_      size_t  Length,
    _In_      Flags_t Flags,
    _Out_     void**  MemoryOut)
{
	if (!Length || !MemoryOut) {
		return OsInvalidParameters;
	}
	return Syscall_MemoryAllocate(Hint, Length, Flags, MemoryOut);
}

OsStatus_t
MemoryFree(
	_In_ void*  Memory,
	_In_ size_t Length)
{
	if (!Length || !Memory) {
		return OsInvalidParameters;
	}
	return Syscall_MemoryFree(Memory, Length);
}

OsStatus_t
MemoryProtect(
    _In_  void*     Memory,
	_In_  size_t    Length,
    _In_  Flags_t   Flags,
    _Out_ Flags_t*  PreviousFlags)
{
	if (!Length || !Memory) {
		return OsInvalidParameters;
	}
    return Syscall_MemoryProtect(Memory, Length, Flags, PreviousFlags);
}
