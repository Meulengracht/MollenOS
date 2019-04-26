/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * Generic Data Structures
 */

#ifndef __DATASTRUCTURES__
#define __DATASTRUCTURES__

#include <os/osdefs.h>

typedef struct {
    union {
        int     Integer;
        UUId_t  Id;
        struct {
            const char* Pointer;
            size_t      Length;
        } String;
    } Value;
} DataKey_t;

typedef enum {
    KeyInteger,
    KeyId,
    KeyString
} KeyType_t;

typedef struct {
    _Atomic(int) SyncObject;
    unsigned     Flags;
} SafeMemoryLock_t;

CRTDECL(void*, dsalloc(size_t size));
CRTDECL(void,  dsfree(void* pointer));

CRTDECL(void, dslock(SafeMemoryLock_t* lock));
CRTDECL(void, dsunlock(SafeMemoryLock_t* lock));

CRTDECL(void, dstrace(const char* fmt, ...));
CRTDECL(void, dswarning(const char* fmt, ...));
CRTDECL(void, dserror(const char* fmt, ...));

/* Helper Function 
 * Matches two keys based on the key type returns 0 if they are equal, or -1 if not */
CRTDECL(int, dsmatchkey(KeyType_t type, DataKey_t key1, DataKey_t key2));

/* Helper Function
 * Used by sorting, it compares to values and returns 
 *  - 1 if 1 > 2, 
 *  - 0 if 1 == 2 and
 *  - -1 if 2 > 1 */
CRTDECL(int, dssortkey(KeyType_t type, DataKey_t key1, DataKey_t key2));

#endif //!__DATASTRUCTURES__
