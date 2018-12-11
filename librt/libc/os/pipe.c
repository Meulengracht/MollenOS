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
 * MollenOS Pipe Communication Interface
 */

#include <os/syscall.h>
#include <os/ipc/ipc.h>
#include <os/ipc/pipe.h>
#include <signal.h>
#include <assert.h>

OsStatus_t
CreatePipe(
    _In_  int     Type,
    _Out_ UUId_t* Handle)
{
    assert(Handle != NULL);
	return Syscall_CreatePipe(Type, Handle);
}

OsStatus_t
DestroyPipe(
    _In_ UUId_t Handle)
{
	return Syscall_DestroyPipe(Handle);
}

OsStatus_t
ReadPipe(
    _In_ UUId_t Handle,
    _In_ void*  Buffer,
    _In_ size_t Length)
{
    assert(Buffer != NULL);
    assert(Length > 0);
	return Syscall_ReadPipe(Handle, Buffer, Length);
}

OsStatus_t
WritePipe(
    _In_ UUId_t Handle,
    _In_ void*  Buffer,
    _In_ size_t Length)
{
    assert(Buffer != NULL);
    assert(Length > 0);
	return Syscall_WritePipe(Handle, Buffer, Length);
}
