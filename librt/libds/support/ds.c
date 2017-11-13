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
 * MollenOS - Generic Data Structures (Shared)
 */

/* Includes 
 * - Library */
#include <ds/ds.h>
#include <string.h>

#ifdef LIBC_KERNEL
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
		case KeyInteger: {
			if (Key1.Value == Key2.Value)
				return 0;
		} break;
		case KeyPointer: {
			if (Key1.Pointer == Key2.Pointer)
				return 0;
		} break;
		case KeyString: {
			return strcmp(Key1.String, Key2.String);
		} break;
	}
	return -1;
}

/* Helper Function
 * Used by sorting, it compares to values
 * and returns 1 if 1 > 2, 0 if 1 == 2 and
 * -1 if 2 > 1 */
int
dssortkey(
    _In_ KeyType_t KeyType,
    _In_ DataKey_t Key1,
    _In_ DataKey_t Key2)
{
	switch (KeyType) {
		case KeyInteger: {
			if (Key1.Value == Key2.Value)
				return 0;
			else if (Key1.Value > Key2.Value)
				return 1;
			else
				return -1;
		} break;
		case KeyPointer: {
			return 0;
		} break;
		case KeyString: {
			return strcmp(Key1.String, Key2.String);
		} break;
	}
	return 0;
}
