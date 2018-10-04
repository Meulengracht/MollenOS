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
 * MollenOS MCore - File Definitions & Structures
 * - This header describes the base file-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _FILE_STRUCTURES_INTERFACE_H_
#define _FILE_STRUCTURES_INTERFACE_H_

#ifndef _CONTRACT_FILESYSTEM_INTERFACE_H_
#error "You must include filesystem.h and not this directly"
#endif

#include <os/mollenos.h>
#include <ds/mstring.h>

/* The shared filesystem entry structure
 * Used as a file-definition by the filemanager and the loaded filesystem modules */
PACKED_TYPESTRUCT(FileSystemEntry, {
    OsFileDescriptor_t      Descriptor;
    MString_t*              Path;
    MString_t*              Name;
    size_t                  Hash;
    UUId_t                  IsLocked;
    int                     References;
    uintptr_t*              System;
});

/* This is the per-handle entry instance
 * structure, so multiple handles can be opened
 * on just a single entry, it refers to a entry structure */
PACKED_TYPESTRUCT(FileSystemEntryHandle, {
    FileSystemEntry_t*  Entry;
    UUId_t              Id;
    UUId_t              Owner;
    Flags_t             Access;
    Flags_t             Options;
    Flags_t             LastOperation;
    uint64_t            Position;
    void*               OutBuffer;
    size_t              OutBufferPosition;
});

#endif //!_FILE_STRUCTURES_INTERFACE_H_
