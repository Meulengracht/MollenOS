/**
 * MollenOS
 *
 * Copyright 2020, Philip Meulengracht
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
 * Debug Definitions & Structures
 * - This header describes debug structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <ddk/debug.h>
#include <internal/_syscalls.h>

oserr_t MapThreadMemoryRegion(
        _In_  uuid_t    threadHandle,
        _In_  uintptr_t address,
        _Out_ void**    topOfStack,
        _Out_ void**    pointerOut)
{
    if (!pointerOut || !topOfStack) {
        return OS_EINVALPARAMS;
    }

    return Syscall_MapThreadMemoryRegion(threadHandle, address, topOfStack, pointerOut);
}
