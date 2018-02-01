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

_CODE_BEGIN
/* Pipe - Open
 * Opens a new communication pipe on the given
 * port for this process, if one already exists
 * SIGPIPE is signaled */
CRTDECL(OsStatus_t, PipeOpen(int Port));

/* Pipe - Close
 * Closes an existing communication pipe on the given
 * port for this process, if one doesn't exists
 * SIGPIPE is signaled */
CRTDECL(OsStatus_t, PipeClose(int Port));

/* Pipe - Read
 * This returns -1 if something went wrong reading
 * a message from the message queue, otherwise it returns 0
 * and fills the structures with information about the message */
CRTDECL(OsStatus_t, PipeRead(int Port, void *Buffer, size_t Length));

/* Pipe send + recieve
 * The send and recieve calls can actually be used for reading extern pipes
 * and send to external pipes */
CRTDECL(OsStatus_t, PipeSend(UUId_t ProcessId, int Port, void *Buffer, size_t Length));
CRTDECL(OsStatus_t, PipeReceive(UUId_t ProcessId, int Port, void *Buffer, size_t Length));
_CODE_END

#endif //!KERNEL_API
#endif //!_MOLLENOS_PIPE_H_
