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
 * MollenOS - Generic Data Structures (Shared)
 */

#include <ds/ds.h>
#include <string.h>

#ifdef LIBC_KERNEL
#include <system/interrupts.h>
#include <heap.h>
#else
#include <stdlib.h>
#endif

/* dsalloc
 * Seperate portable memory allocator for data-structures */
void*
dsalloc(
    _In_ size_t size)
{
#ifdef LIBC_KERNEL
	return kmalloc(size);
#else
	return malloc(size);
#endif
}

/* dsfree
 * Seperate portable memory freeing for data-structures */
void
dsfree(
    _In_ void *p)
{
#ifdef LIBC_KERNEL
	kfree(p);
#else
	free(p);
#endif
}

/* dslock
 * Acquires the lock given, this is a blocking call and will wait untill
 * the lock is acquired. */
void
dslock(
    _In_ SafeMemoryLock_t *MemoryLock)
{
    bool locked = true;

#ifdef LIBC_KERNEL
    MemoryLock->Flags = InterruptDisable();
#endif
    while (1) {
        bool val = atomic_exchange(&MemoryLock->SyncObject, locked);
        if (val == false) {
            break;
        }
    }
}

/* dsunlock
 * Releases the lock given and restores any previous flags. */
void
dsunlock(
    _In_ SafeMemoryLock_t *MemoryLock)
{
    atomic_exchange(&MemoryLock->SyncObject, false);
#ifdef LIBC_KERNEL
    InterruptRestoreState(MemoryLock->Flags);
#endif
}

/* Helper Function
 * Matches two keys based on the key type
 * returns 0 if they are equal, or -1 if not */
int
dsmatchkey(
    _In_ KeyType_t KeyType,
    _In_ DataKey_t Key1,
    _In_ DataKey_t Key2)
{
	switch (KeyType) {
        case KeyId: {
			if (Key1.Value.Id == Key2.Value.Id) {
                return 0;
            }
        } break;
		case KeyInteger: {
			if (Key1.Value.Integer == Key2.Value.Integer) {
                return 0;
            }
		} break;
		case KeyString: {
			return strcmp(Key1.Value.String.Pointer, Key2.Value.String.Pointer);
		} break;
	}
	return -1;
}

/* Helper Function
 * Used by sorting, it compares to values
 * and returns 1 if 1 > 2, 0 if 1 == 2 and -1 if 2 > 1 */
int
dssortkey(
    _In_ KeyType_t KeyType,
    _In_ DataKey_t Key1,
    _In_ DataKey_t Key2)
{
	switch (KeyType) {
        case KeyId: {
			if (Key1.Value.Id == Key2.Value.Id)
				return 0;
			else if (Key1.Value.Id > Key2.Value.Id)
				return 1;
			else
				return -1;
        } break;
		case KeyInteger: {
			if (Key1.Value.Integer == Key2.Value.Integer)
				return 0;
			else if (Key1.Value.Integer > Key2.Value.Integer)
				return 1;
			else
				return -1;
		} break;
		case KeyString: {
			return strcmp(Key1.Value.String.Pointer, Key2.Value.String.Pointer);
		} break;
	}
	return 0;
}
