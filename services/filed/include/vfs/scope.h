/**
 * Copyright 2021, Philip Meulengracht
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

#ifndef __SCOPE_H__
#define __SCOPE_H__

#include <os/osdefs.h>

/**
 * @brief Initializes the filesystem scope subsystem
 */
extern void VFSScopeInitialize(void*, void*);

/**
 * @brief Retrieves the relevant filesystem scope for the given process
 * @param processId
 * @return
 */
extern struct VFS*
VFSScopeGet(
        _In_ uuid_t processId);

#endif //!__SCOPE_H__
