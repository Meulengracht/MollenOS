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

#ifndef __VFS_FILESYSTEM_HANDLE_H__
#define __VFS_FILESYSTEM_HANDLE_H__

#include <os/osdefs.h>
#include <ddk/filesystem.h>
#include "cache.h"

typedef struct FileSystemHandle {
    FileSystemHandleBase_t* base;
    element_t               header;
    UUId_t                  id;
    UUId_t                  owner;
    unsigned int            last_operation;
    FileSystemCacheEntry_t* entry;
} FileSystemHandle_t;

/**
 * Initializes the filesystem handle subsystem
 */
extern void VfsHandleInitialize(void);

/**
 * @brief
 *
 * @param entry
 * @param handleOut
 * @return
 */
extern oscode_t
VfsHandleCreate(
        _In_  UUId_t                  processId,
        _In_  FileSystemCacheEntry_t* entry,
        _In_  unsigned int            options,
        _In_  unsigned int            access,
        _Out_ FileSystemHandle_t**    handleOut);

/**
 * @brief
 *
 * @param handle
 * @return
 */
extern oscode_t
VfsHandleDestroy(
        _In_ UUId_t              processId,
        _In_ FileSystemHandle_t* handle);

/**
 * @brief
 *
 * @param processId
 * @param handleId
 * @param requiredAccess
 * @param handleOut
 * @return
 */
extern oscode_t
VfsHandleAccess(
        _In_  UUId_t               processId,
        _In_  UUId_t               handleId,
        _In_  unsigned int         requiredAccess,
        _Out_ FileSystemHandle_t** handleOut);

#endif //!__VFS_FILESYSTEM_HANDLE_H__
