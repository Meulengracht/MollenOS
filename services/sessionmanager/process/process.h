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
 * MollenOS Service - Session Manager
 * - Contains the implementation of the session-manager which keeps track
 *   of all users and their running applications.
 */

#ifndef __PROCESS_INTERFACE__
#define __PROCESS_INTERFACE__

#include <os/osdefs.h>
#include <os/process.h>

typedef struct _SessionProcess {
    UUId_t                      MainThread;
    UUId_t                      Id;

    const char*                 Name;
    const char*                 Path;
    UUId_t                      MemorySpace;
    ProcessStartupInformation_t StartupInformation;

    void*                       Executable;
    uintptr_t                   NextLoadingAddress;
    uint8_t*                    FileBuffer;
    size_t                      FileBufferLength;
    int                         Code;
} SessionProcess_t;

/* CreateProcess
 * Spawns a new process, which can be configured through the parameters. */
OsStatus_t
CreateProcess(
    _In_  const char*                  Path,
    _In_  ProcessStartupInformation_t* Parameters,
    _Out_ UUId_t*                      Handle);

#endif //!__PROCESS_INTERFACE__
