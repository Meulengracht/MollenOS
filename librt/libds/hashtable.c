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

#include <ds/hashtable.h>
#include <assert.h>
#include <string.h>

static size_t 
GetDefaultHashValue(const char* Value, size_t Length)
{
    return 0;
}

/* HashTableCreate
 * Initializes a new hash table structure of the desired capacity, and load factor.
 * The load factor defaults to HASHTABLE_DEFAULT_LOADFACTOR. */
HashTable_t*
HashTableCreate(
    _In_ size_t Capacity,
    _In_ size_t LoadFactor)
{
    HashTable_t* HashTable = (HashTable_t*)dsalloc(sizeof(HashTable_t));
    assert(HashTable != NULL);

    HashTable->Array = (Collection_t*)dsalloc(sizeof(Collection_t) * Capacity);
    assert(HashTable->Array != NULL);
    memset(HashTable->Array, 0, sizeof(sizeof(Collection_t) * Capacity));
    for (size_t i = 0; i < Capacity; i++) {
        HashTable->Array[i].KeyType = KeyId;
    }

    HashTable->Size         = 0;
    HashTable->Capacity     = Capacity;
    HashTable->LoadFactor   = LoadFactor;
    HashTable->GetHashCode  = GetDefaultHashValue;
	return HashTable;
}

/* HashTableDestroy
 * Cleans up all resources associated with the hashtable. This does not clear up the values
 * registered in the hash-table. */
void
HashTableDestroy(
    _In_ HashTable_t* HashTable)
{
    assert(HashTable != NULL);
    for (size_t i = 0; i < HashTable->Capacity; i++) {
        CollectionClear(&HashTable->Array[i]);
    }
    dsfree(HashTable->Array);
    dsfree(HashTable);
}

/* HashTableSetHashFunction
 * Overrides the default hash function with a user provided hash function. To
 * reset this set with NULL. */
void
HashTableSetHashFunction(
    _In_ HashTable_t*   HashTable,
    _In_ HashFn         Fn)
{
    assert(HashTable != NULL);
    HashTable->GetHashCode = Fn;
}

/* HashTableInsert
 * Inserts or overwrites the existing key in the hashtable. */
void
HashTableInsert(
    _In_ HashTable_t*   HashTable,
    _In_ DataKey_t      Key,
    _In_ void*          Data)
{
    assert(HashTable != NULL);
    size_t              ArrayIndex  = GetDefaultHashValue(Key.Value.String.Pointer, Key.Value.String.Length) % HashTable->Capacity;
    Collection_t*       Array       = &HashTable->Array[ArrayIndex];
    DataKey_t           ArrayKey    = { .Value.Id = ArrayIndex };
    CollectionItem_t*   Existing    = CollectionGetNodeByKey(Array, ArrayKey, 0);
    if (Existing == NULL) {
        CollectionAppend(Array, CollectionCreateNode(ArrayKey, Data));
    }
    else {
        Existing->Data = Data;
    }
}

/* HashTableRemove 
 * Removes the entry with the matching key from the hashtable. */
void
HashTableRemove(
    _In_ HashTable_t*   HashTable,
    _In_ DataKey_t      Key)
{
    assert(HashTable != NULL);
    size_t              ArrayIndex  = GetDefaultHashValue(Key.Value.String.Pointer, Key.Value.String.Length) % HashTable->Capacity;
    Collection_t*       Array       = &HashTable->Array[ArrayIndex];
    DataKey_t           ArrayKey    = { .Value.Id = ArrayIndex };
    CollectionRemoveByKey(Array, ArrayKey);
}

/* HashTableGetValue
 * Retrieves the data associated with the given key from the hashtable */
void*
HashTableGetValue(
    _In_ HashTable_t*   HashTable,
    _In_ DataKey_t      Key)
{
    assert(HashTable != NULL);
    size_t              ArrayIndex  = GetDefaultHashValue(Key.Value.String.Pointer, Key.Value.String.Length) % HashTable->Capacity;
    Collection_t*       Array       = &HashTable->Array[ArrayIndex];
    DataKey_t           ArrayKey    = { .Value.Id = ArrayIndex };
	return CollectionGetDataByKey(Array, ArrayKey, 0);
}
