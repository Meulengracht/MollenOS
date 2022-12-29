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
 * @brief
 * @param path
 * @param at
 * @param type
 * @param flags
 * @return
 */
CRTDECL(oserr_t,
OSMount(
        _In_ const char*  path,
        _In_ const char*  at,
        _In_ const char*  type,
        _In_ unsigned int flags));

/**
 * @brief
 * @param path
 * @return
 */
CRTDECL(oserr_t,
OSUnmount(
        _In_ const char* path));

_CODE_END
#endif //!__OS_SERVICE_MOUNT_H__
