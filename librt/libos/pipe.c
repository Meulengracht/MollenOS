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
 * - C-Library */
#include <signal.h>

/* IPC - Open - Pipe
 * Opens a new communication pipe on the given
 * port for this process, if one already exists
 * SIGPIPE is signaled */
int PipeOpen(int Port)
{
	/* Variables */
	int Result = 0;

	/* Open is rather just calling the underlying syscall */
	Result = Syscall1(SYSCALL_OPENPIPE, SYSCALL_PARAM(Port));

	/* Sanitize the return parameters */
	if (Result < 0) {
		raise(SIGPIPE);
	}

	/* Done! */
	return Port;
}

/* IPC - Close - Pipe
 * Closes an existing communication pipe on the given
 * port for this process, if one doesn't exists
 * SIGPIPE is signaled */
int PipeClose(int Port)
{
	/* Variables */
	int Result = 0;

	/* Close is rather just calling the underlying syscall */
	Result = Syscall1(SYSCALL_OPENPIPE, SYSCALL_PARAM(Port));

	/* Sanitize the return parameters */
	if (Result < 0) {
		raise(SIGPIPE);
	}

	/* Done! */
	return Result;
}

/* IPC - Read
 * This returns -1 if something went wrong reading
 * a message from the message queue, otherwise it returns 0
 * and fills the structures with information about the message */
int PipeRead(int Pipe, void *Buffer, size_t Length)
{
	/* Variables */
	int Result = 0;

	/* Sanitize length */
	if (Length == 0) {
		return Result;
	}

	/* Read is rather just calling the underlying syscall */
	Result = Syscall4(SYSCALL_READPIPE, SYSCALL_PARAM(Pipe),
		SYSCALL_PARAM(Buffer), SYSCALL_PARAM(Length), 0);

	/* Sanitize the return parameters */
	if (Result < 0) {
		raise(SIGPIPE);
	}

	/* Done! */
	return Result;
}

/* IPC - Send
 * Returns -1 if message failed to send
 * Returns -2 if message-target didn't exist
 * Returns 0 if message was sent correctly to target */
int PipeSend(UUId_t Target, int Port, void *Message, size_t Length)
{
	/* Variables */
	int Result = 0;

	/* Sanitize length */
	if (Length == 0) {
		return Result;
	}

	/* Send is rather just calling the underlying syscall */
	Result = Syscall4(SYSCALL_WRITEPIPE, SYSCALL_PARAM(Target),
		SYSCALL_PARAM(Port), SYSCALL_PARAM(Message), SYSCALL_PARAM(Length));

	/* Sanitize the return parameters */
	if (Result < 0) {
		raise(SIGPIPE);
	}

	/* Done! */
	return Result;
}
