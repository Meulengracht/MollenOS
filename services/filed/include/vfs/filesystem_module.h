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

#ifndef __VFS_FILESYSTEM_MODULE_H__
#define __VFS_FILESYSTEM_MODULE_H__

#include <ds/list.h>
#include <ddk/filesystem.h>
#include <os/osdefs.h>
#include <os/usched/mutex.h>
#include "filesystem_types.h"

typedef struct FileSystemModule {
    element_t           header;
    enum FileSystemType type;
    int                 references;
    Handle_t            handle;
    struct usched_mtx   lock;

    FsInitialize_t      Initialize;
    FsDestroy_t         Destroy;
    FsOpenEntry_t       OpenEntry;
    FsCreatePath_t      CreatePath;
    FsCloseEntry_t      CloseEntry;
    FsDeleteEntry_t     DeleteEntry;
    FsChangeFileSize_t  ChangeFileSize;
    FsOpenHandle_t      OpenHandle;
    FsCloseHandle_t     CloseHandle;
    FsReadEntry_t       ReadEntry;
    FsWriteEntry_t      WriteEntry;
    FsSeekInEntry_t     SeekInEntry;
} FileSystemModule_t;

/**
 * @brief Loads the appropriate filesystem driver for given type.
 *
 * @param type [In] The type of filesystem to load.
 * @return     A handle for the given filesystem driver.
 */
extern FileSystemModule_t*
VfsLoadModule(
        _In_ enum FileSystemType type);

/**
 * @brief Unloads the given module if its reference count reaches 0.
 *
 * @param module [In] The module to release a reference on.
 */
extern void
VfsUnloadModule(
        _In_ FileSystemModule_t* module);

#endif //!__VFS_FILESYSTEM_MODULE_H__
