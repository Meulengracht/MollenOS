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

#define __TRACE
#include <ddk/memory.h>
#include <ddk/utils.h>
#include <internal/_syscalls.h>
#include <module.h>
#include "private.h"
#include "pe.h"

static uintptr_t          g_systemBaseAddress = 0;

uintptr_t
PeImplGetBaseAddress(void)
{
    if (g_systemBaseAddress == 0) {
        Syscall_GetProcessBaseAddress(&g_systemBaseAddress);
    }
    return g_systemBaseAddress;
}

oserr_t
PeImplCreateImageSpace(
        _Out_ uuid_t* handleOut)
{
    uuid_t  memorySpaceHandle = UUID_INVALID;
    oserr_t osStatus          = CreateMemorySpace(0, &memorySpaceHandle);
    if (osStatus != OS_EOK) {
        return osStatus;
    }
    *handleOut = memorySpaceHandle;
    return OS_EOK;
}

struct PEImageLoadContext*
PEImageLoadContextNew(
        _In_ uuid_t scope,
        _In_ char*  paths)
{

}

void
PEImageLoadContextDelete(
        _In_ struct PEImageLoadContext* loadContext)
{

}
