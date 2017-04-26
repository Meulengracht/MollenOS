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

#ifndef _MOLLENOS_PIPE_H_
#define _MOLLENOS_PIPE_H_

/* Do a kernel guard as similar functions
 * exists in kernel but for kernel stuff */
#ifndef _KAPI

/* Includes 
 * - System */
#include <os/osdefs.h>

/* Define this here to allow for c++ interfacing */
_CODE_BEGIN

/* Pipe - Open
 * Opens a new communication pipe on the given
 * port for this process, if one already exists
 * SIGPIPE is signaled */
MOSAPI 
UUId_t 
PipeOpen(
	_In_ int Port);

/* Pipe - Close
 * Closes an existing communication pipe on the given
 * port for this process, if one doesn't exists
 * SIGPIPE is signaled */
MOSAPI 
OsStatus_t 
PipeClose(
	_In_ UUId_t Pipe);

/* Pipe - Read
 * This returns -1 if something went wrong reading
 * a message from the message queue, otherwise it returns 0
 * and fills the structures with information about the message */
MOSAPI 
OsStatus_t 
PipeRead(
	_In_ UUId_t Pipe, 
	_In_ void *Buffer, 
	_In_ size_t Length);

/* Pipe - Send
 * Returns -1 if message failed to send
 * Returns -2 if message-target didn't exist
 * Returns 0 if message was sent correctly to target */
MOSAPI 
OsStatus_t 
PipeSend(
	_In_ UUId_t Target, 
	_In_ int Port, 
	_In_ void *Message, 
	_In_ size_t Length);

_CODE_END

#endif //!KERNEL_API
#endif //!_MOLLENOS_PIPE_H_
