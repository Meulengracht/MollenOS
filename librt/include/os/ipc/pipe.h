/* MollenOS
 *
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS Pipe Communication Interface
 */

#ifndef __PIPE_INTERFACE__
#define __PIPE_INTERFACE__

#include <os/osdefs.h>

#define PIPE_RAW            0   // Raw data passing
#define PIPE_STRUCTURED     1   // This should always be used when there are multiple readers or producers

_CODE_BEGIN
/* OpenPipe
 * Opens a new communication pipe on the given port for this process, 
 * if one already exists SIGPIPE is signaled */
CRTDECL(
OsStatus_t,
OpenPipe(
    _In_ int    Port, 
    _In_ int    Type));

/* ClosePipe
 * Closes an existing communication pipe on the given port for this process, 
 * if one doesn't exists SIGPIPE is signaled */
CRTDECL(
OsStatus_t,
ClosePipe(
    _In_ int    Port));

/* ReadPipe
 * This returns -1 if something went wrong reading
 * a message from the message queue, otherwise it returns 0
 * and fills the structures with information about the message */
CRTDECL(
OsStatus_t,
ReadPipe(
    _In_ int    Port,
    _In_ void*  Buffer,
    _In_ size_t Length));

/* Pipe send + recieve
 * The send and recieve calls can actually be used for reading extern pipes
 * and send to external pipes */
CRTDECL(
OsStatus_t,
SendPipe(
    _In_ UUId_t ProcessId,
    _In_ int    Port,
    _In_ void*  Buffer,
    _In_ size_t Length));

CRTDECL(
OsStatus_t,
ReceivePipe(
    _In_ UUId_t ProcessId,
    _In_ int    Port,
    _In_ void*  Buffer,
    _In_ size_t Length));
_CODE_END

#endif //!__PIPE_INTERFACE__
