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
 *enum FileSystemType
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __VFS_FILESYSTEM_TYPES_H__
#define __VFS_FILESYSTEM_TYPES_H__

enum FileSystemType {
    FileSystemType_UNKNOWN,
    FileSystemType_FAT,
    FileSystemType_EXFAT,
    FileSystemType_NTFS,
    FileSystemType_HFS,
    FileSystemType_HPFS,
    FileSystemType_MFS,
    FileSystemType_EXT
};

#endif //!__VFS_FILESYSTEM_TYPES_H__
