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

#include <ddk/ddkdefs.h>

#define PIPE_RAW            0   // Raw data passing
#define PIPE_STRUCTURED     1   // This should always be used when there are multiple readers or producers

_CODE_BEGIN
/* CreatePipe
 * Creates a new communication pipe that can be used for transferring arbitrary data. */
DDKDECL(
OsStatus_t,
CreatePipe(
    _In_  int     Type,
    _Out_ UUId_t* Handle));

/* DestroyPipe
 * Closes an existing communication pipe and invalidates it for further data. */
DDKDECL(
OsStatus_t,
DestroyPipe(
    _In_ UUId_t Handle));

/* ReadPipe
 * This returns -1 if something went wrong reading
 * a message from the message queue, otherwise it returns 0
 * and fills the structures with information about the message */
DDKDECL(
OsStatus_t,
ReadPipe(
    _In_ UUId_t Handle,
    _In_ void*  Buffer,
    _In_ size_t Length));

/* WritePipe
 * Writes the provided data by length to the pipe handle. */
DDKDECL(
OsStatus_t,
WritePipe(
    _In_ UUId_t Handle,
    _In_ void*  Buffer,
    _In_ size_t Length));
_CODE_END

#endif //!__PIPE_INTERFACE__
