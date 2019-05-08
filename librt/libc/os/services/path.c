/* MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * Path Service Definitions & Structures
 * - This header describes the base path-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <ddk/services/file.h>
#include <os/services/path.h>
#include <os/services/targets.h>

OsStatus_t
PathResolveEnvironment(
    _In_  EnvironmentPath_t Base,
    _Out_ char*             Buffer,
    _In_  size_t            MaxLength)
{
    MRemoteCall_t Request;

    RPCInitialize(&Request, __FILEMANAGER_TARGET, 
        __FILEMANAGER_INTERFACE_VERSION, __FILEMANAGER_PATHRESOLVE);
    RPCSetArgument(&Request, 0, (const void*)&Base, sizeof(EnvironmentPath_t));
    RPCSetResult(&Request, (const void*)Buffer, MaxLength);
    return RPCExecute(&Request);
}

OsStatus_t
PathCanonicalize(
    _In_  const char* Path,
    _Out_ char*       Buffer,
    _In_  size_t      MaxLength)
{
    MRemoteCall_t Request;

    RPCInitialize(&Request, __FILEMANAGER_TARGET, 
        __FILEMANAGER_INTERFACE_VERSION, __FILEMANAGER_PATHCANONICALIZE);
    RPCSetArgument(&Request, 0, (const void*)Path, strlen(Path) + 1);
    RPCSetResult(&Request, (const void*)Buffer, MaxLength);
    return RPCExecute(&Request);
}
