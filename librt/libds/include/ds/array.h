/**
 * MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * Managed Array Implementation
 *  - Implements a managed array that can auto expand
 */

#ifndef __DS_ARRAY_H__
#define __DS_ARRAY_H__

#include <os/osdefs.h>
#include <ds/ds.h>

typedef struct {
    Flags_t          Flags;
    size_t           Capacity;
    SafeMemoryLock_t SyncObject;
    size_t           LookAt;
    _Atomic(void**)  Elements;
} Array_t;

#define ARRAY_CAN_EXPAND 0x1

#define ARRAY_GET(Array, Index)          ((Index < Array->Capacity) ? atomic_load(&Array->Elements)[Index] : NULL)
#define ARRAY_SET(Array, Index, Element) if (Index < Array->Capacity) atomic_load(&Array->Elements)[Index] = Element

_CODE_BEGIN

CRTDECL(OsStatus_t, ArrayCreate(Flags_t Flags, size_t Capacity, Array_t** ArrayOut));
CRTDECL(void,       ArrayDestroy(Array_t* Array));
CRTDECL(UUId_t,     ArrayAppend(Array_t* Array, void* Element));
CRTDECL(void,       ArrayRemove(Array_t* Array, UUId_t Index));

_CODE_END

#endif //!__DS_ARRAY_H__
