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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Virtual FileSystem Definitions & Structures
 * - This header describes the base virtual filesystem-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __VFS_CACHE_H__
#define __VFS_CACHE_H__

#include <ds/mstring.h>
#include <ddk/filesystem.h>
#include "filesystem.h"

typedef struct FileSystemCacheEntry {
    FileSystemEntryBase_t* base;
    FileSystem_t*          filesystem;
    MString_t*             path;

    struct usched_mtx      lock;
    int                    references;
    list_t                 handles;
} FileSystemCacheEntry_t;

/**
 * @brief Retrieves a file entry from cache, otherwise it is opened or created depending on options passed.
 *
 * @param fileSystem [In]  The filesystem that the path belongs to.
 * @param path       [In]  Subpath of the entry to open/create
 * @param options    [In]  Open/creation options.
 * @param entryOut   [Out] Pointer to storage where the pointer will be stored.
 * @return           Status of the operation
 */
extern OsStatus_t
VfsFileSystemCacheGet(
        _In_  FileSystem_t*            fileSystem,
        _In_  MString_t*               subPath,
        _In_  unsigned int             options,
        _Out_ FileSystemCacheEntry_t** entryOut);

/**
 * @brief Removes a file path from the cache if it exists.
 *
 * @param path [In] The path of the file to remove.
 */
extern void
VfsFileSystemCacheRemove(
        _In_  FileSystem_t* fileSystem,
        _In_  MString_t*    subPath);

#endif //!__VFS_CACHE_H__
