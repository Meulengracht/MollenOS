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

#ifndef __VFS_MODULE_H__
#define __VFS_MODULE_H__

#include <ds/list.h>
#include <ddk/filesystem.h>
#include <os/osdefs.h>
#include <os/usched/mutex.h>
#include "filesystem_types.h"

struct VFSOperations {
    FsInitialize_t      Initialize;
    FsDestroy_t         Destroy;
    FsStat_t            Stat;

    FsOpen_t            Open;
    FsClose_t           Close;
    FsLink_t            Link;
    FsUnlink_t          Unlink;
    FsReadLink_t        ReadLink;
    FsCreate_t          Create;
    FsMove_t            Move;

    FsTruncate_t        Truncate;
    FsRead_t            Read;
    FsWrite_t           Write;
    FsSeek_t            Seek;
};

struct VFSModule {
    enum FileSystemType  Type;
    int                  References;
    Handle_t             Handle;
    struct usched_mtx    Lock;
    struct VFSOperations Operations;
};

/**
 * @brief Loads the appropriate filesystem driver for given type.
 *
 * @param type [In] The type of filesystem to load.
 * @return     A handle for the given filesystem driver.
 */
extern oscode_t
VfsLoadModule(
        _In_  enum FileSystemType type,
        _Out_ struct VFSModule**  moduleOut);

/**
 * @brief Unloads the given module if its reference count reaches 0.
 *
 * @param module [In] The module to release a reference on.
 */
extern void
VfsUnloadModule(
        _In_ struct VFSModule* module);

#endif //!__VFS_MODULE_H__
