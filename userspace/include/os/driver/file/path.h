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
 * MollenOS MCore - Path Utilities Definitions & Structures
 * - This header describes the filesystem path-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _PATH_INTERFACE_H_
#define _PATH_INTERFACE_H_

#ifndef _CONTRACT_FILESYSTEM_INTERFACE_H_
#error "You must include filesystem.h and not this directly"
#endif

/* Includes
 * - System */
#include <os/osdefs.h>
#include <os/driver/file.h>

/* PathResolveEnvironment
 * Resolves the given env-path identifier to a string
 * that can be used to locate files. */
#ifdef __FILEMANAGER_IMPL
__EXTERN 
MString_t*
SERVICEABI
PathResolveEnvironment(
	_In_ EnvironmentPath_t Base);
#else
SERVICEAPI
OsStatus_t
SERVICEABI
PathResolveEnvironment(
	_In_ EnvironmentPath_t Base,
	_Out_ char *Buffer,
	_In_ size_t MaxLength)
{
	/* Variables */
	MRemoteCall_t Request;

	/* RPC Usage */
	RPCInitialize(&Request, __FILEMANAGER_INTERFACE_VERSION,
		PIPE_RPCOUT, __FILEMANAGER_PATHRESOLVE);
	RPCSetArgument(&Request, 0, (__CONST void*)&Base, sizeof(EnvironmentPath_t));
	RPCSetResult(&Request, (__CONST void*)Buffer, MaxLength);
	return RPCExecute(&Request, __FILEMANAGER_TARGET);
}
#endif

/* PathCanonicalize
 * Canonicalizes the path by removing extra characters
 * and resolving all identifiers in path */
#ifdef __FILEMANAGER_IMPL
__EXTERN 
MString_t*
SERVICEABI
PathCanonicalize(
	_In_ EnvironmentPath_t Base,
	_In_ __CONST char *Path);
#else
SERVICEAPI
OsStatus_t
SERVICEABI
PathCanonicalize(
	_In_ EnvironmentPath_t Base,
	_In_ char *Path,
	_Out_ char *Buffer,
	_In_ size_t MaxLength)
{
	/* Variables */
	MRemoteCall_t Request;

	/* RPC Usage */
	RPCInitialize(&Request, __FILEMANAGER_INTERFACE_VERSION,
		PIPE_RPCOUT, __FILEMANAGER_PATHCANONICALIZE);
	RPCSetArgument(&Request, 0, (__CONST void*)&Base, sizeof(EnvironmentPath_t));
	RPCSetArgument(&Request, 1, (__CONST void*)Path, strlen(Path));
	RPCSetResult(&Request, (__CONST void*)Buffer, MaxLength);
	return RPCExecute(&Request, __FILEMANAGER_TARGET);
}
#endif

#endif //!_PATH_INTERFACE_H_
