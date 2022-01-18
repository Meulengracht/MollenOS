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
 * Vali Initrd Filesystem
 * - Contains the implementation of the Vali Initrd Filesystem.
 *   This filesystem is used to store the initrd of the kernel.
 */

#include <errno.h>
#include "private.h"

struct VaFsDirectoryReader {
    
};

struct VaFsDirectoryWriter {
    
};

int vafs_directory_create_root(
    struct VaFs*          vafs,
    struct VaFsDirecory** directoryOut)
{
    struct VaFsDirectory* directory;
    int                   status;
    
    if (vafs == NULL || directoryOut == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    directory = (struct VaFsDirectory*)malloc(sizeof(struct VaFsDirectory));
    if (!directory) {
        errno = ENOMEM;
        return -1;
    }
    memset(directory, 0, sizeof(struct VaFsDirectory));

    *directoryOut = directory;
    return 0;
}

int vafs_directory_open_root(
    struct VaFs*           vafs,
    struct VaFsDirectory** directoryOut)
{

}

int vafs_opendir(
    void*       handle,
    const char* path,
    void**      handleOut)
{

}

int vafs_dir_read(
    void*          handle,
    struct dirent* entry)
{

}

int vafs_dir_write_file(
    void*       handle,
    const char* name,
    void*       content,
    size_t      size)
{

}

int vafs_dir_write_directory(
    void*       handle,
    const char* name,
    void**      handleOut)
{

}

int vafs_dir_close(
    void* handle)
{

}
