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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS MCore - Memory Support Definitions & Structures
 * - This header describes the base memory-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <internal/_syscalls.h>
#include <ddk/memory.h>

oserr_t
CreateMemorySpace(
        _In_  unsigned int Flags,
        _Out_ uuid_t* Handle)
{
    if (Handle == NULL) {
        return OS_EUNKNOWN;
    }
    return Syscall_CreateMemorySpace(Flags, Handle);
}

oserr_t
GetMemorySpaceForThread(
        _In_  uuid_t  Thread,
        _Out_ uuid_t* Handle)
{
    if (Handle == NULL) {
        return OS_EUNKNOWN;
    }
    return Syscall_GetMemorySpaceForThread(Thread, Handle);
}

oserr_t
CreateMemoryMapping(
        _In_  uuid_t                          Handle,
        _In_  struct MemoryMappingParameters* Parameters,
        _Out_ void**                          AddressOut)
{
    if (Parameters == NULL || AddressOut == NULL) {
        return OS_EUNKNOWN;
    }
    return Syscall_CreateMemorySpaceMapping(Handle, Parameters, AddressOut);
}
