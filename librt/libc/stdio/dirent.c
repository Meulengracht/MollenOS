/* MollenOS
 *
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS - C Standard Library
 * - Directory functionality implementation
 */
//#define __TRACE

#include <os/utils.h>
#include <os/file.h>
#include <stdlib.h>
#include <errno.h>
#include <io.h>

/* mkdir
 * Creates a new directory for the entire path. */
int
mkdir(
    _In_ const char*    path,
    _In_ int            mode)
{
    int fd;
    _CRT_UNUSED(mode);

    // Validate the input
    if (path == NULL) {
        _set_errno(EINVAL);
        return -1;
    }

    fd = open(path, O_CREAT | O_EXCL | O_RDWR | O_RECURS | O_DIR, 0);
    if (fd != -1) {
        close(fd);
        fd = 0;
    }
    return fd;
}

/* opendir
 * Opens or creates a new directory and returns a handle to it. */
int
opendir(
    _In_  const char*   path,
    _In_  int           flags,
    _Out_ struct DIR**  handle)
{
    int fd = open(path, flags, 0);
    if (fd == -1) {
        return -1;
    }

    // Setup out
    *handle = (struct DIR*)malloc(sizeof(struct DIR));
    (*handle)->d_handle = fd;
    (*handle)->d_index  = 0;
    return 0;
}

/* closedir
 * Closes a directory handle. Releases any resources and frees the handle. */
int
closedir(
    _In_ struct DIR *handle)
{
    if (handle != NULL) {
        if (close(handle->d_handle) != -1) {
            free(handle);
            return 0;
        }
        return -1;
    }
    _set_errno(EINVAL);
    return -1;
}

/* readdir
 * Reads a directory entry at the current index and increases the current index
 * for the directory handle. */
int
readdir(
    _In_ struct DIR*    handle, 
    _In_ struct DIRENT* entry)
{
    if (handle != NULL && entry != NULL) {
        int bytes_read = read(handle->d_handle, (void*)entry, sizeof(struct DIRENT));
        if (bytes_read == sizeof(struct DIRENT)) {
            handle->d_index++;
            return 0;
        }
        _set_errno(ENODATA);
        return -1;
    }
    _set_errno(EINVAL);
    return -1;
}
