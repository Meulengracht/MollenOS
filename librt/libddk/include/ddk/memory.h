/**
 * Copyright 2023, Philip Meulengracht
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
 */

#ifndef __DDK_MEMORY_H__
#define __DDK_MEMORY_H__

#include <ddk/ddkdefs.h>

struct MemoryMappingParameters {
    uintptr_t    VirtualAddress;
    size_t       Length;
    unsigned int Flags;
};

/**
 * @brief Creates a new memory space that can be used to create new mappings, and manipulate existing mappings.
 */
DDKDECL(oserr_t,
CreateMemorySpace(
    _In_  unsigned int Flags,
    _Out_ uuid_t*      Handle));

/**
 * @brief Retrieves the memory space that is currently running for the thread handle.
 */
DDKDECL(oserr_t,
GetMemorySpaceForThread(
    _In_  uuid_t  Thread,
    _Out_ uuid_t* Handle));

/**
 * CreateMemoryMapping
 * Creates a new memory mapping in the memory space, if it was successful in creating a mapping,
 * a new access buffer for that piece of memory will be returned.
 */
DDKDECL(oserr_t,
CreateMemoryMapping(
    _In_  uuid_t                          Handle,
    _In_  struct MemoryMappingParameters* Parameters,
    _Out_ void**                          AddressOut));

#endif //!__DDK_MEMORY_H__
