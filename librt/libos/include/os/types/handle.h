/**
 * Copyright 2023, Philip Meulengracht
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

#ifndef __OS_TYPES_HANDLE_H__
#define	__OS_TYPES_HANDLE_H__

#include <os/osdefs.h>

enum OSHandleType {
    OSHANDLE_NULL,
    OSHANDLE_FILE,
    OSHANDLE_EVENT,
    OSHANDLE_HQUEUE,
    OSHANDLE_SHM,
    OSHANDLE_SOCKET,
    __OSHANDLE_COUNT
};

// OSHandle represents a kernel handle which can be shared
// across processes. Members in this structure must be completely
// *flat* if they need to be shared. Members in this structure
// must also be immutable.
typedef struct OSHandle {
    // Members that will be serialized
    uuid_t   ID;
    uint16_t Type;
    uint16_t Flags;

    // Not serialized, kept locally.

    // Payload is an implementation-specific pointer that
    // may point to additional data that needs to be
    // serialized or deserialized. This value is immutable
    // and cannot change once added.
    void* Payload;
} OSHandle_t;

#define __HEADER_SIZE_RAW (sizeof(uuid_t) + sizeof(uint16_t) + sizeof(uint16_t))

typedef struct OSHandleOps {
    // Destroy function can override the default behaviour
    // of destroying a system handle. The default destroy
    // behaviour is to just free the global system handle. If
    // the handle needs a custom method of freeing, then destroy
    // must be overriden. Destroy is responsible for freeing the
    // global system handle.
    void   (*Destroy)(struct OSHandle*);
    size_t (*Serialize)(struct OSHandle*, void*);
    size_t (*Deserialize)(struct OSHandle*, const void*);
} OSHandleOps_t;

#endif //!__OS_TYPES_HANDLE_H__
