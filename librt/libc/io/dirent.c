/**
 * Copyright 2017, Philip Meulengracht
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

#include <errno.h>
#include <internal/_dirent.h>
#include <internal/_io.h>
#include <io.h>
#include <os/services/file.h>
#include <os/mollenos.h>
#include <os/handle.h>
#include <string.h>
#include <stdlib.h>

int
mkdir(
    _In_ const char*  path,
    _In_ unsigned int mode)
{
    return OsErrToErrNo(
            OSMakeDirectory(
                    path,
                    _fperms(mode)
            )
    );
}

static struct DIR*
__DIR_new(
        _In_ OSHandle_t* handle)
{
    struct DIR* dir = malloc(sizeof(struct DIR));
    if (!dir) {
        return NULL;
    }
    memset(dir, 0, sizeof(struct DIR));
    memcpy(&dir->_handle, handle, sizeof(OSHandle_t));
    return dir;
}

struct DIR*
opendir(
    _In_  const char* path)
{
    struct DIR* dir;
    OSHandle_t  handle;
    oserr_t     oserr = OSOpenPath(
            path,
            __FILE_DIRECTORY,
            0,
            &handle
    );
    if (oserr != OS_EOK) {
        OsErrToErrNo(oserr);
        return NULL;
    }

    dir = __DIR_new(&handle);
    if (dir == NULL) {
        OSHandleDestroy(&handle);
        return NULL;
    }
    return dir;
}

int
closedir(
    _In_ struct DIR* dir)
{
    if (dir == NULL) {
        _set_errno(EINVAL);
        return -1;
    }

    OSHandleDestroy(&dir->_handle);
    free(dir);
    return 0;
}

static int __ToDirentType(unsigned int flags)
{
    switch (FILE_FLAG_TYPE(flags)) {
        case FILE_FLAG_FILE: return DT_REG;
        case FILE_FLAG_LINK: return DT_LNK;
        case FILE_FLAG_DIRECTORY: return DT_DIR;
        default: return DT_UNKNOWN;
    }
}

static void __ToDirent(OSDirectoryEntry_t* in, struct dirent* out)
{
    size_t nameLength = strlen(in->Name);

    out->d_ino = in->ID;
    out->d_off = in->Index;
    out->d_reclen = (sizeof(struct dirent) - NAME_MAX) + nameLength;
    out->d_type = __ToDirentType(in->Flags);
    strncpy(&out->d_name[0], in->Name, NAME_MAX);
}

struct dirent*
readdir(
    _In_ struct DIR* dir)
{
    OSDirectoryEntry_t entry;
    oserr_t            oserr;

    if (dir == NULL) {
        errno = EINVAL;
        return NULL;
    }

    oserr = OSReadDirectory(dir->_handle.ID, &entry);
    if (oserr != OS_EOK) {
        OsErrToErrNo(oserr);
        return NULL;
    }
    __ToDirent(&entry, &dir->_cdirent);
    free((void*)entry.Name);
    return &dir->_cdirent;
}

int rewinddir(struct DIR* dir)
{
    return seekdir(dir, 0);
}

int seekdir(struct DIR* dir, long index)
{
    UInteger64_t position = { .QuadPart = (uint64_t)index };
    if (dir == NULL || index < 0) {
        errno = EINVAL;
        return -1;
    }
    return OsErrToErrNo(OSSeekFile(dir->_handle.ID, &position));
}

long telldir(struct DIR* dir)
{
    UInteger64_t position;
    oserr_t      oserr;
    if (dir == NULL) {
        errno = EINVAL;
        return -1;
    }

    oserr = OSGetFilePosition(dir->_handle.ID, &position);
    if (oserr != OS_EOK) {
        return OsErrToErrNo(oserr);
    }
    return (long)position.u.LowPart;
}
