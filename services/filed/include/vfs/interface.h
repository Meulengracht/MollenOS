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

typedef oserr_t (*FsInitialize_t)(struct VFSInterface*, struct VFSStorageParameters*, void** instanceData);
typedef oserr_t (*FsDestroy_t)(struct VFSInterface*, void* instanceData, unsigned int unmountFlags);
typedef oserr_t (*FsOpen_t)(struct VFSInterface*, void* instanceData, mstring_t* path, void** dataOut);
typedef oserr_t (*FsCreate_t)(struct VFSInterface*, void* instanceData, void* data, mstring_t* name, uint32_t owner, uint32_t flags, uint32_t permissions, void** dataOut);
typedef oserr_t (*FsClose_t)(struct VFSInterface*, void* instanceData, void* data);
typedef oserr_t (*FsStat_t)(struct VFSInterface*, void* instanceData, struct VFSStatFS*);
typedef oserr_t (*FsLink_t)(struct VFSInterface*, void* instanceData, void* data, mstring_t* linkName, mstring_t* linkTarget, int symbolic);
typedef oserr_t (*FsUnlink_t)(struct VFSInterface*, void* instanceData, mstring_t* path);
typedef oserr_t (*FsReadLink_t)(struct VFSInterface*, void* instanceData, mstring_t* path, mstring_t** pathOut);
typedef oserr_t (*FsMove_t)(struct VFSInterface*, void* instanceData, mstring_t* from, mstring_t* to, int copy);
typedef oserr_t (*FsRead_t)(struct VFSInterface*, void* instanceData, void* data, uuid_t bufferHandle, size_t bufferOffset, size_t unitCount, size_t* unitsRead);
typedef oserr_t (*FsWrite_t)(struct VFSInterface*, void* instanceData, void* data, uuid_t bufferHandle, size_t bufferOffset, size_t unitCount, size_t* unitsWritten);
typedef oserr_t (*FsTruncate_t)(struct VFSInterface*, void* instanceData, void* data, uint64_t size);
typedef oserr_t (*FsSeek_t)(struct VFSInterface*, void* instanceData, void* data, uint64_t absolutePosition, uint64_t* absolutePositionOut);

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
    Handle_t             Handle;
    uuid_t               DriverID;
    struct usched_mtx    Lock;
    struct VFSOperations Operations;
};

/**
 * @brief
 * @return
 */
extern struct VFSInterface*
MemFSNewInterface(void);

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
        _In_  const char*            type,
        _Out_ struct VFSInterface**  interfaceOut);

/**
 * @brief
 * @param interfaceDriverID
 * @param interfaceOut
 * @return
 */
extern oserr_t
VFSInterfaceLoadDriver(
        _In_  uuid_t                 interfaceDriverID,
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
