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
 */

#ifndef __VFS_STAT_H__
#define __VFS_STAT_H__

#include <ds/mstring.h>
#include <os/types/time.h>

struct VFSStat {
    uuid_t ID;
    uuid_t StorageID;

    mstring_t* Name;
    mstring_t* LinkTarget;
    uint32_t   Owner;
    uint32_t   Permissions; // Permissions come from os/file/types.h
    uint32_t   Flags;       // Flags come from os/file/types.h
    uint64_t   Size;
    uint32_t   Links;

    OSTimestamp_t Accessed;
    OSTimestamp_t Modified;
    OSTimestamp_t Created;
};

#endif //!__VFS_STAT_H__
