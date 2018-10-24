/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * - Generic Hash Table Implementation
 *  The hash-table uses chained indices to solve the possibility of collision.
 *  The hash table runs in O(1 + a) time, where a = size/capacity assuming
 *  optimal hash-distribution. All list sizes should be <a>
 */

#ifndef __GENERIC_HASHTABLE_H__
#define __GENERIC_HASHTABLE_H__

#include <os/osdefs.h>
#include <ds/ds.h>
#include <ds/collection.h>

#define HASHTABLE_DEFAULT_LOADFACTOR    75 // Equals 75 percent

typedef size_t(*HashFn)(void* Value);

typedef struct _HashTable {
    size_t          Capacity;
    size_t          Size;
    size_t          LoadFactor;
    HashFn          GetHashCode;
    Collection_t**  Array;
} HashTable_t;

/* HashTableCreate
 * Initializes a new hash table structure of the desired capacity, and load factor.
 * The load factor defaults to HASHTABLE_DEFAULT_LOADFACTOR. */
CRTDECL(HashTable_t*,
HashTableCreate(
    _In_ size_t Capacity,
    _In_ size_t LoadFactor));

/* HashTableSetHashFunction
 * Overrides the default hash function with a user provided hash function. To
 * reset this set with NULL. */

/* HashTableDestroy
 * Releases all resources 
 * associated with the hashtable */
CRTDECL(void,
HashTableDestroy(
    _In_ HashTable_t *HashTable));

/* HashTableInsert
 * Inserts an object with the given
 * string key from the hash table */
CRTDECL(void,
HashTableInsert(
    _In_ HashTable_t *HashTable, 
    _In_ DataKey_t Key, 
    _In_ void *Data));

/* HashTableRemove 
 * Removes an object with the given 
 * string key from the hash table */
CRTDECL(void,
HashTableRemove(
    _In_ HashTable_t *HashTable, 
    _In_ DataKey_t Key));

/* HashTableGetValue
 * Retrieves the data associated with
 * a value from the hash table */
CRTDECL(void*,
HashTableGetValue(
    _In_ HashTable_t *HashTable, 
    _In_ DataKey_t Key));

#endif //!_HASHTABLE_H_
