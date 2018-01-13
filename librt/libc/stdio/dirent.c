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

/* Includes
 * - System */
#include <os/driver/file.h>
#include <stdlib.h>
#include <errno.h>
#include <io.h>

/* _opendir
 * Opens or creates a new directory and returns a handle to it. */
int
_opendir(
    _In_  const char*               path,
    _In_  int                       flags,
    _Out_ struct directory_handle** handle)
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
    if (flags & _O_CREAT) {
        OpenFlags |= __DIRECTORY_CREATE;
        if (flags & _O_EXCL) {
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
    *handle = (struct directory_handle*)malloc(sizeof(struct directory_handle));
    (*handle)->d_handle = FileHandle;
    (*handle)->d_index  = -1;
    return 0;
}

/* _closedir
 * Closes a directory handle. Releases any resources and frees the handle. */
int
_closedir(
    _In_ struct directory_handle *handle)
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

/* _readdir
 * Reads a directory entry at the current index and increases the current index
 * for the directory handle. */
int
_readdir(
    _In_ struct directory_handle*   handle, 
    _In_ struct directory_entry*    entry)
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
    memset(entry, 0, sizeof(struct directory_entry));
    entry->d_type = DirEntry.FileInformation.Flags;
    memcpy(entry->d_name, &DirEntry.FileName[0], strlen(&DirEntry.FileName[0]));
    return 0;
}
