/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
    // Variables
    FileSystemCode_t Code   = FsOk;
    Flags_t OpenFlags       = __DIRECTORY_CREATE | __DIRECTORY_FAILONEXIST;
    UUId_t FileHandle       = UUID_INVALID;
    _CRT_UNUSED(mode);

    // Validate the input
    if (path == NULL) {
        _set_errno(EINVAL);
        return -1;
    }

    // Invoke service
    Code = OpenDirectory(path, OpenFlags, 
        __FILE_READ_ACCESS | __FILE_WRITE_ACCESS, &FileHandle);
    if (_fval(Code) == -1) {
        return -1;
    }
    Code = CloseDirectory(FileHandle);
    return _fval(Code);
}

/* opendir
 * Opens or creates a new directory and returns a handle to it. */
int
opendir(
    _In_  const char*   path,
    _In_  int           flags,
    _Out_ struct DIR**  handle)
{
    // Variables
    FileSystemCode_t Code   = FsOk;
    Flags_t OpenFlags       = 0;
    UUId_t FileHandle       = UUID_INVALID;

    // Validate the input
    if (path == NULL || handle == NULL) {
        _set_errno(EINVAL);
        return -1;
    }

    // Convert open flags
    if (flags & O_CREAT) {
        OpenFlags |= __DIRECTORY_CREATE;
        if (flags & O_EXCL) {
            OpenFlags |= __DIRECTORY_FAILONEXIST;
        }
    }
    else {
        OpenFlags |= __DIRECTORY_MUSTEXIST;
    }

    // Invoke service
    Code = OpenDirectory(path, OpenFlags, 
        __FILE_READ_ACCESS | __FILE_WRITE_ACCESS, &FileHandle);
    if (_fval(Code) == -1) {
        return -1;
    }

    // Setup out
    *handle = (struct DIR*)malloc(sizeof(struct DIR));
    (*handle)->d_handle = FileHandle;
    (*handle)->d_index  = -1;
    return 0;
}

/* closedir
 * Closes a directory handle. Releases any resources and frees the handle. */
int
closedir(
    _In_ struct DIR *handle)
{
    // Variables
    FileSystemCode_t Code   = FsOk;

    // Validate the input
    if (handle == NULL) {
        _set_errno(EINVAL);
        return -1;
    }

    // Invoke service
    Code = CloseDirectory(handle->d_handle);
    if (_fval(Code) == -1) {
        return -1;
    }
    free(handle);
    return 0;
}

/* readdir
 * Reads a directory entry at the current index and increases the current index
 * for the directory handle. */
int
readdir(
    _In_ struct DIR*    handle, 
    _In_ struct DIRENT* entry)
{
    // Variables
    ReadDirectoryPackage_t DirEntry;
    FileSystemCode_t Code   = FsOk;

    // Validate the input
    if (handle == NULL || entry == NULL) {
        _set_errno(EINVAL);
        return -1;
    }

    // Invoke service
    Code = ReadDirectory(handle->d_handle, &DirEntry);
    if (_fval(Code) == -1) {
        return -1;
    }

    // Convert to c structure
    handle->d_index++;
    memset(entry, 0, sizeof(struct DIRENT));
    entry->d_type = DirEntry.FileInformation.Flags;
    memcpy(entry->d_name, &DirEntry.FileName[0], strlen(&DirEntry.FileName[0]));
    return 0;
}
