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

/* OpenPipe
 * Opens a new communication pipe on the given port for this process, 
 * if one already exists SIGPIPE is signaled */
OsStatus_t
OpenPipe(
    _In_ int    Port, 
    _In_ int    Type)
{
    assert(Port >= 0);
	return Syscall_PipeOpen(Port, Type);
}

/* ClosePipe
 * Closes an existing communication pipe on the given port for this process, 
 * if one doesn't exists SIGPIPE is signaled */
OsStatus_t
ClosePipe(
    _In_ int    Port)
{
    assert(Port >= 0);
	return Syscall_PipeClose(Port);
}

/* PipeRead
 * This returns -1 if something went wrong reading
 * a message from the message queue, otherwise it returns 0
 * and fills the structures with information about the message */
OsStatus_t
ReadPipe(
    _In_ int    Port,
    _In_ void*  Buffer,
    _In_ size_t Length)
{
	// Sanitize input
    assert(Port >= 0);
    assert(Buffer != NULL);
    assert(Length > 0);
	return Syscall_PipeRead(Port, Buffer, Length);
}

/* Pipe send + recieve
 * The send and recieve calls can actually be used for reading extern pipes
 * and send to external pipes */
OsStatus_t
SendPipe(
    _In_ UUId_t ProcessId,
    _In_ int    Port,
    _In_ void*  Buffer,
    _In_ size_t Length)
{
	// Sanitize input
    assert(Port >= 0);
    assert(Buffer != NULL);
    assert(Length > 0);
	return Syscall_PipeSend(ProcessId, Port, Buffer, Length);
}

/* Pipe send + recieve
 * The send and recieve calls can actually be used for reading extern pipes
 * and send to external pipes */
OsStatus_t
ReceivePipe(
    _In_ UUId_t ProcessId,
    _In_ int    Port,
    _In_ void*  Buffer,
    _In_ size_t Length)
{
    assert(Port >= 0);
    assert(Buffer != NULL);
    assert(Length > 0);
    
    if (ProcessId == UUID_INVALID && Port != PIPE_STDIN) {
        return ReadPipe(Port, Buffer, Length);
    }
	return Syscall_PipeReceive(ProcessId, Port, Buffer, Length);
}
