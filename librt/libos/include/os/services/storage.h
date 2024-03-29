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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Storage Service Definitions & Structures
 * - This header describes the base storage-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __SERVICES_STORAGE_H__
#define __SERVICES_STORAGE_H__

#include <os/types/storage.h>

/* QueryStorageByPath
 * Queries information about a storage medium that belong to the given file path. */
CRTDECL(oscode_t,
QueryStorageByPath(
    _In_ const char*            Path,
    _In_ OsStorageDescriptor_t* StorageDescriptor));

/* QueryStorageByHandle
 * Queries information about a storage medium that belong to the given file handle. */
CRTDECL(oscode_t,
QueryStorageByHandle(
    _In_ UUId_t                 Handle,
    _In_ OsStorageDescriptor_t* StorageDescriptor));

#endif //!__SERVICES_DISK_H__
