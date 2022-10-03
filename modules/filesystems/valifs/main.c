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
 */

//#define __TRACE

#include <ddk/utils.h>
#include <fs/common.h>
#include <stdlib.h>
#include <string.h>
#include <vafs/vafs.h>

struct __ValiFSContext {
    struct VFSStorageParameters Storage;
    UInteger64_t                Position;
};

static long __ValiFS_Seek(void* userData, long offset, int whence);
static int  __ValiFS_Read(void* userData, void*, size_t, size_t*);

static struct VaFsOperations g_vafsOperations = {
        .seek = __ValiFS_Seek,
        .read = __ValiFS_Read,
        .write = NULL,  // We only support reading from this driver
        .close = NULL,  // We handle close ourselves
};

oserr_t
FsInitialize(
        _In_  struct VFSStorageParameters* storageParameters,
        _Out_ void**                       instanceData)
{

}

oserr_t
FsDestroy(
        _In_ void*         instanceData,
        _In_ unsigned int  unmountFlags)
{

}

oserr_t
FsOpen(
        _In_      void*      instanceData,
        _In_      mstring_t* path,
        _Out_Opt_ void**     dataOut)
{

}

oserr_t
FsCreate(
        _In_  void*      instanceData,
        _In_  void*      data,
        _In_  mstring_t* name,
        _In_  uint32_t   owner,
        _In_  uint32_t   flags,
        _In_  uint32_t   permissions,
        _Out_ void**     dataOut)
{

}

oserr_t
FsClose(
        _In_ void* instanceData,
        _In_ void* data)
{

}

__FSAPI oserr_t
__FSDECL(FsStat)(
        _In_ void*             instanceData,
        _In_ struct VFSStatFS* stat);

__FSAPI oserr_t
__FSDECL(FsLink)(
        _In_ void*      instanceData,
        _In_ void*      data,
        _In_ mstring_t* linkName,
        _In_ mstring_t* linkTarget,
        _In_ int        symbolic);

__FSAPI oserr_t
__FSDECL(FsUnlink)(
        _In_ void*      instanceData,
        _In_ mstring_t* path);

__FSAPI oserr_t
__FSDECL(FsReadLink)(
        _In_ void*      instanceData,
        _In_ mstring_t* path,
        _In_ mstring_t* pathOut);

__FSAPI oserr_t
__FSDECL(FsMove)(
        _In_ void*      instanceData,
        _In_ mstring_t* from,
        _In_ mstring_t* to,
        _In_ int        copy);

__FSAPI oserr_t
__FSDECL(FsRead)(
        _In_  void*   instanceData,
        _In_  void*   data,
        _In_  uuid_t  bufferHandle,
        _In_  void*   buffer,
        _In_  size_t  bufferOffset,
        _In_  size_t  unitCount,
        _Out_ size_t* unitsRead);

__FSAPI oserr_t
__FSDECL(FsWrite)(
        _In_  void*   instanceData,
        _In_  void*   data,
        _In_  uuid_t  bufferHandle,
        _In_  void*   buffer,
        _In_  size_t  bufferOffset,
        _In_  size_t  unitCount,
        _Out_ size_t* unitsWritten);

__FSAPI oserr_t
__FSDECL(FsTruncate)(
        _In_ void*    instanceData,
        _In_ void*    data,
        _In_ uint64_t size);

__FSAPI oserr_t
__FSDECL(FsSeek)(
        _In_  void*     instanceData,
        _In_  void*     data,
        _In_  uint64_t  absolutePosition,
        _Out_ uint64_t* absolutePositionOut);


static long __ValiFS_Seek(void* userData, long offset, int whence)
{

}

static int  __ValiFS_Read(void* userData, void*, size_t, size_t*)
{

}
