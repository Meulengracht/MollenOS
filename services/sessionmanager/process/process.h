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

typedef struct _SystemProcess {
    UUId_t                  MainThread;
    UUId_t                  Id;

    const char*             Name;
    const char*             Path;
    UUId_t                  MemorySpace;

    // Below is everything related to
    // the startup and the executable information
    // that the Ash has
    void*                   Executable;
    uintptr_t               NextLoadingAddress;
    uint8_t*                FileBuffer;
    size_t                  FileBufferLength;
    int                     Code;
} SystemProcess_t;




#endif //!__PROCESS_INTERFACE__
