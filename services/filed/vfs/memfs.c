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
 */

#include <ddk/utils.h>
#include <ds/hashtable.h>
#include <vfs/vfs.h>
#include "private.h"

static oserr_t
__MemFSInitialize(
        _In_ struct VFSCommonData* vfsCommonData)
{
    return OsOK;
}

static oserr_t
__MemFSDestroy(
        _In_ struct VFSCommonData* vfsCommonData,
        _In_ unsigned int          unmountFlags)
{
    return OsOK;
}

static oserr_t
__MemFSOpen(
        _In_      struct VFSCommonData* vfsCommonData,
        _In_      mstring_t*            path,
        _Out_Opt_ void**                dataOut)
{
    return OsOK;
}

static oserr_t
__MemFSCreate(
        _In_  struct VFSCommonData* vfsCommonData,
        _In_  void*                 data,
        _In_  mstring_t*            name,
        _In_  uint32_t              owner,
        _In_  uint32_t              flags,
        _In_  uint32_t              permissions,
        _Out_ void**                dataOut)
{
    return OsOK;
}

static oserr_t
__MemFSClose(
        _In_ struct VFSCommonData* vfsCommonData,
        _In_ void*                 data)
{
    return OsOK;
}

static oserr_t
__MemFSStat(
        _In_ struct VFSCommonData* vfsCommonData,
        _In_ struct VFSStatFS*     stat)
{
    return OsOK;
}

static oserr_t
__MemFSLink(
        _In_ struct VFSCommonData* vfsCommonData,
        _In_ void*                 data,
        _In_ mstring_t*            linkName,
        _In_ mstring_t*            linkTarget,
        _In_ int                   symbolic)
{
    return OsOK;
}

static oserr_t
__MemFSUnlink(
        _In_ struct VFSCommonData* vfsCommonData,
        _In_ mstring_t*            path)
{
    return OsOK;
}

static oserr_t
__MemFSReadLink(
        _In_ struct VFSCommonData* vfsCommonData,
        _In_ mstring_t*            path,
        _In_ mstring_t*            pathOut)
{
    return OsOK;
}

static oserr_t
__MemFSMove(
        _In_ struct VFSCommonData* vfsCommonData,
        _In_ mstring_t*            from,
        _In_ mstring_t*            to,
        _In_ int                   copy)
{
    return OsOK;
}

static oserr_t
__MemFSRead(
        _In_  struct VFSCommonData* vfsCommonData,
        _In_  void*                 data,
        _In_  uuid_t                bufferHandle,
        _In_  void*                 buffer,
        _In_  size_t                bufferOffset,
        _In_  size_t                unitCount,
        _Out_ size_t*               unitsRead)
{
    return OsOK;
}

static oserr_t
__MemFSWrite(
        _In_  struct VFSCommonData* vfsCommonData,
        _In_  void*                 data,
        _In_  uuid_t                bufferHandle,
        _In_  void*                 buffer,
        _In_  size_t                bufferOffset,
        _In_  size_t                unitCount,
        _Out_ size_t*               unitsWritten)
{
    return OsOK;
}

static oserr_t
__MemFSTruncate(
        _In_ struct VFSCommonData* vfsCommonData,
        _In_ void*                 data,
        _In_ uint64_t              size)
{
    return OsOK;
}

static oserr_t
__MemFSSeek(
        _In_  struct VFSCommonData* vfsCommonData,
        _In_  void*                 data,
        _In_  uint64_t              absolutePosition,
        _Out_ uint64_t*             absolutePositionOut)
{
    return OsOK;
}

static struct VFSOperations g_memfsOperations = {
        .Initialize = __MemFSInitialize,
        .Destroy = __MemFSDestroy,
        .Stat = __MemFSStat,
        .Open = __MemFSOpen,
        .Close = __MemFSClose,
        .Link = __MemFSLink,
        .Unlink = __MemFSUnlink,
        .ReadLink = __MemFSReadLink,
        .Create = __MemFSCreate,
        .Move = __MemFSMove,
        .Truncate = __MemFSTruncate,
        .Read = __MemFSRead,
        .Write = __MemFSWrite,
        .Seek = __MemFSSeek
};

struct VFSInterface* MemFSNewInterface(void) {
    return VFSInterfaceNew(FileSystemType_MEMFS, NULL, &g_memfsOperations);
}