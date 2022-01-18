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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Threads Definitions & Structures
 * - This header describes the base threads-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <os/mollenos.h>
#include <internal/_syscalls.h>

void
InitializeThreadParameters(
    _In_ ThreadParameters_t* Paramaters)
{
    Paramaters->Name              = NULL;
    Paramaters->Configuration     = 0;
    Paramaters->MemorySpaceHandle = UUID_INVALID;
    Paramaters->MaximumStackSize  = __MASK;
}

OsStatus_t 
SetCurrentThreadName(
    _In_ const char* ThreadName)
{
    return Syscall_ThreadSetCurrentName(ThreadName);
}

OsStatus_t 
GetCurrentThreadName(
    _In_ char*  ThreadNameBuffer,
    _In_ size_t MaxLength)
{
    return Syscall_ThreadGetCurrentName(ThreadNameBuffer, MaxLength);
}
