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
UUId_t
PipeOpen(
    _In_ int Port)
{
	// Variables
	OsStatus_t Result = OsError;

	// Sanitize input
	if (Port < 0) {
		return UUID_INVALID;
	}

    // Execute system call and verify
	Result = Syscall_PipeOpen(Port, 0);
	if (Result != OsSuccess) {
		raise(SIGPIPE);
	}
	return (UUId_t)Port;
}

/* PipeClose
 * Closes an existing communication pipe on the given
 * port for this process, if one doesn't exists
 * SIGPIPE is signaled */
OsStatus_t
PipeClose(
    _In_ UUId_t Pipe)
{
	// Variables
	OsStatus_t Result = OsError;

	// Sanitize input
	if (Pipe == UUID_INVALID) {
		return OsError;
	}

	// Execute system call and verify
	Result = Syscall_PipeClose(Pipe);
	if (Result != OsSuccess) {
		raise(SIGPIPE);
	}
	return Result;
}

/* PipeRead
 * This returns -1 if something went wrong reading
 * a message from the message queue, otherwise it returns 0
 * and fills the structures with information about the message */
OsStatus_t
PipeRead(
    _In_ UUId_t  Pipe,
    _In_ void   *Buffer,
    _In_ size_t  Length)
{
	// Variables
	OsStatus_t Result = OsError;

	// Sanitize input
	if (Pipe == UUID_INVALID || Length == 0) {
		return OsError;
	}

	// Execute system call and verify
	Result = Syscall_PipeRead(Pipe, Buffer, Length);
	if (Result != OsSuccess) {
		raise(SIGPIPE);
	}
	return Result;
}

/* PipeSend
 * Returns -1 if message failed to send
 * Returns -2 if message-target didn't exist
 * Returns 0 if message was sent correctly to target */
OsStatus_t
PipeSend(
    _In_ UUId_t  Target,
    _In_ int     Port,
    _In_ void   *Message,
    _In_ size_t  Length)
{
	// Variables
	OsStatus_t Result = OsError;

	// Sanitize input
	if (Length == 0) {
		return OsError;
	}

	// Execute system call and verify
	Result = Syscall_PipeWrite(Target, Port, Message, Length);
	if (Result != OsSuccess) {
		raise(SIGPIPE);
	}
	return Result;
}
