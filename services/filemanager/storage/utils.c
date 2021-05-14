/**
 * MollenOS
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
 * File Manager Service
 * - Handles all file related services and disk services
 */

#include "../include/vfs.h"
#include <sys_storage_service.h>

static int g_diskTable[__FILEMANAGER_MAXDISKS] = { 0 };

UUId_t
VfsIdentifierAllocate(
        _In_ FileSystemDisk_t* disk)
{
    int indexBegin = 0;
    int indexEnd;
    int i;

    // Start out by determing start index
    indexEnd = __FILEMANAGER_MAXDISKS / 2;
    if (disk->flags & SYS_STORAGE_FLAGS_REMOVABLE) {
        indexBegin = __FILEMANAGER_MAXDISKS / 2;
        indexEnd   = __FILEMANAGER_MAXDISKS;
    }

    // Now iterate the range for the type of disk
    for (i = indexBegin; i < indexEnd; i++) {
        if (g_diskTable[i] == 0) {
            g_diskTable[i] = 1;
            return (UUId_t)(i - indexBegin);
        }
    }
    return UUID_INVALID;
}

void
VfsIdentifierFree(
        _In_ FileSystemDisk_t* disk,
        _In_ UUId_t            id)
{
    int index = (int)id;
    if (disk->flags & SYS_STORAGE_FLAGS_REMOVABLE) {
        index += __FILEMANAGER_MAXDISKS / 2;
    }
    if (index < __FILEMANAGER_MAXDISKS) {
        g_diskTable[index] = 0;
    }
}
