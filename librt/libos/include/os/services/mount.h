/**
 * Copyright 2022, Philip Meulengracht
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
 * Mount Service Definitions & Structures
 * - This header describes the base structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __OS_SERVICE_MOUNT_H__
#define __OS_SERVICE_MOUNT_H__

#include <os/osdefs.h>
#include <os/types/mount.h>
#include <ds/mstring.h>

_CODE_BEGIN

/**
 * @brief Mounts a file system at a path.
 * @param path Device or source path for the file system.
 * @param at Target path at which to mount the file system.
 * @param type File-system type name.
 * @param flags Mount behavior flags.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSMount(
        _In_ const char*  path,
        _In_ const char*  at,
        _In_ const char*  type,
        _In_ unsigned int flags));

/**
 * @brief Unmounts the file system mounted at a path.
 * @param path Mount point to remove.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
OSUnmount(
        _In_ const char* path));

_CODE_END
#endif //!__OS_SERVICE_MOUNT_H__
