/**
 * Copyright 2022, Philip Meulengracht
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
 */

#ifndef __OS_MEMORY_H__
#define __OS_MEMORY_H__

#include <os/osdefs.h>
#include <os/types/memory.h>

_CODE_BEGIN

/**
 * @brief
 * @param Hint
 * @param Length
 * @param Flags
 * @param MemoryOut
 * @return
 */
CRTDECL(oserr_t,
MemoryAllocate(
        _In_  void*        Hint,
        _In_  size_t       Length,
        _In_  unsigned int Flags,
        _Out_ void**       MemoryOut));

/**
 * @brief
 * @param Memory
 * @param Length
 * @return
 */
CRTDECL(oserr_t,
MemoryFree(
        _In_ void*  Memory,
        _In_ size_t Length));

/**
 * @brief
 * @param Memory
 * @param Length
 * @param Flags
 * @param PreviousFlags
 * @return
 */
CRTDECL(oserr_t,
MemoryProtect(
        _In_  void*         Memory,
        _In_  size_t        Length,
        _In_  unsigned int  Flags,
        _Out_ unsigned int* PreviousFlags));

/**
 * @brief
 * @param Memory
 * @param DescriptorOut
 * @return
 */
CRTDECL(oserr_t,
MemoryQueryAllocation(
        _In_ void*                 Memory,
        _In_ OsMemoryDescriptor_t* DescriptorOut));

/**
 * @brief
 * @param Memory
 * @param Length
 * @param AttributeArray
 * @return
 */
CRTDECL(oserr_t,
MemoryQueryAttributes(
        _In_ void*         Memory,
        _In_ size_t        Length,
        _In_ unsigned int* AttributeArray));

_CODE_END
#endif //!__OS_MEMORY_H__
