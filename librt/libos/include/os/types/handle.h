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
    OSHANDLE_FILE,
    OSHANDLE_EVENT,
    __OSHANDLE_COUNT
};

// OSHandle represents a kernel handle which can be shared
// across processes. Members in this structure must be completely
// *flat* if they need to be shared. Members in this structure
// must also be immutable.
typedef struct OSHandle {
    // Members that will be serialized
    uuid_t            ID;
    enum OSHandleType Type;

    // Not serialized, kept locally.

    // Payload is a implementation-specific pointer that
    // may point to additional data that needs to be
    // serialized or deserialized. This value is immutable
    // and cannot change once added.
    void* Payload;
} OSHandle_t;

#define __HEADER_SIZE_RAW (sizeof(uuid_t) + sizeof(uint32_t))

typedef void    (*OSHandleDestroyFn)(struct OSHandle*);
typedef oserr_t (*OSHandleSerializeFn)(struct OSHandle*, void*);
typedef oserr_t (*OSHandleDeserializeFn)(struct OSHandle*, const void*);

#endif //!__OS_TYPES_HANDLE_H__
