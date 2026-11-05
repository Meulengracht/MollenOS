/**
 * MollenOS
 *
 * Copyright, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __HANDLES_PATHS_H__
#define __HANDLES_PATHS_H__

#include <os/osdefs.h>
#include <handles/types.h>

/**
 * @brief Registers a global handle path that can be used to look up the handle.
 *
 * @param handleId [In] The handle to register the path with.
 * @param path   [In] The path at which the handle should reside.
 */
KERNELAPI oserr_t KERNELABI
RegisterHandlePath(
    _In_ uuid_t      handleId,
    _In_ const char* path);

/**
 * @brief Tries to resolve a handle from the given path.
 *
 * @param path      [In]  The path to resolve a handle for.
 * @param handleOut [Out] A pointer to handle storage.
 */
KERNELAPI oserr_t KERNELABI
LookupHandleByPath(
    _In_  const char* path,
    _Out_ uuid_t*     handleOut);

#endif //! __HANDLES_PATHS_H__
