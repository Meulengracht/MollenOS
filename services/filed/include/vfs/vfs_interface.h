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

struct VFSInterface {
    enum FileSystemType  Type;
    Handle_t             Handle;
    struct usched_mtx    Lock;
    struct VFSOperations Operations;
};

/**
 * @brief
 * @return
 */
extern struct VFSInterface* MemFSNewInterface(void);

/**
 * @brief
 *
 * @param type
 * @param dllHandle
 * @param operations
 * @return
 */
struct VFSInterface*
VFSInterfaceNew(
        _In_  enum FileSystemType   type,
        _In_  Handle_t              dllHandle,
        _In_  struct VFSOperations* operations);

/**
 * @brief Loads the appropriate filesystem driver for given type.
 *
 * @param type [In] The type of filesystem to load.
 * @return     A handle for the given filesystem driver.
 */
extern oserr_t
VFSInterfaceLoadInternal(
        _In_  enum FileSystemType    type,
        _Out_ struct VFSInterface**  interfaceOut);

/**
 * @brief Unloads the given interface if its reference count reaches 0.
 *
 * @param interface [In] The interface to release a reference on.
 */
extern void
VFSInterfaceDelete(
        _In_ struct VFSInterface* interface);

#endif //!__VFS_MODULE_H__
