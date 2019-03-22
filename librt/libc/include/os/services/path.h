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

#ifndef __SERVICES_PATH_H__
#define __SERVICES_PATH_H__

#include <os/types/path.h>

/* PathResolveEnvironment
 * Resolves the given env-path identifier to a string that can be used to locate files. */
CRTDECL(OsStatus_t,
PathResolveEnvironment(
    _In_ EnvironmentPath_t Base,
    _In_ char*             Buffer,
    _In_ size_t            MaxLength));

/* PathCanonicalize
 * Canonicalizes the path by removing extra characters and resolving all identifiers in path */
CRTDECL(OsStatus_t,
PathCanonicalize(
    _In_ const char* Path,
    _In_ char*       Buffer,
    _In_ size_t      MaxLength));

#endif //!__SERVICES_PATH_H__
