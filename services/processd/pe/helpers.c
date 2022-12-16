/**
 * Copyright 2018, Philip Meulengracht
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
 * PE/COFF Image Loader
 *    - Implements support for loading and processing pe/coff image formats
 *      and implemented as a part of libds to share between services and kernel
 */

#include <ddk/memory.h>
#include <internal/_syscalls.h>
#include "pe.h"
#include <stdio.h>
#include <time.h>

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
