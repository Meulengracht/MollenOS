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

/* Includes
 * - System */
#include <os/syscall.h>
#include <os/ipc/pipe.h>

/* Includes
 * - Library */
#include <signal.h>

/* PipeOpen
 * Opens a new communication pipe on the given
 * port for this process, if one already exists
 * SIGPIPE is signaled */
OsStatus_t
PipeOpen(
    _In_ int Port)
{
	// Sanitize input
	if (Port < 0) {
		return UUID_INVALID;
	}
	return Syscall_PipeOpen(Port, 0);
}

/* PipeClose
 * Closes an existing communication pipe on the given
 * port for this process, if one doesn't exists
 * SIGPIPE is signaled */
OsStatus_t
PipeClose(
    _In_ int Port)
{
	// Sanitize input
	if (Port < 0) {
		return OsError;
	}
	return Syscall_PipeClose(Port);
}

/* PipeRead
 * This returns -1 if something went wrong reading
 * a message from the message queue, otherwise it returns 0
 * and fills the structures with information about the message */
OsStatus_t
PipeRead(
    _In_ int    Port,
    _In_ void*  Buffer,
    _In_ size_t Length)
{
	// Sanitize input
	if (Port < 0 || Buffer == NULL || Length == 0) {
		return OsError;
	}
	return Syscall_PipeRead(Port, Buffer, Length);
}

/* Pipe send + recieve
 * The send and recieve calls can actually be used for reading extern pipes
 * and send to external pipes */
OsStatus_t
PipeSend(
    _In_ UUId_t ProcessId,
    _In_ int    Port,
    _In_ void*  Buffer,
    _In_ size_t Length) {
	// Sanitize input
	if (Port < 0 || Buffer == NULL || Length == 0) {
		return OsError;
	}
	return Syscall_PipeSend(ProcessId, Port, Buffer, Length);
}

/* Pipe send + recieve
 * The send and recieve calls can actually be used for reading extern pipes
 * and send to external pipes */
OsStatus_t
PipeReceive(
    _In_ UUId_t ProcessId,
    _In_ int    Port,
    _In_ void*  Buffer,
    _In_ size_t Length) {
    if (ProcessId == UUID_INVALID) {
        return PipeRead(Port, Buffer, Length);
    }
    // Sanitize input
	if (Port < 0 || Buffer == NULL || Length == 0) {
		return OsError;
	}
	return Syscall_PipeReceive(ProcessId, Port, Buffer, Length);
}
