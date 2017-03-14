/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS MCore - Generic Hash Table
* The hash-table uses chained indices to solve the
* possibility of collision. The hash table runs in
* O(1 + a) time, where a = size/capacity assuming
* optimal hash-distribution. All list sizes should be <a>
*/

/* Includes */
#include <ds/hashtable.h>
#include <stddef.h>
#include <string.h>

/* HashTableCreate
 * Initializes a new hash table
 * of the given capacity */
HashTable_t *HashTableCreate(KeyType_t KeyType, size_t Capacity)
{
	_CRT_UNUSED(KeyType);
	_CRT_UNUSED(Capacity);
	return NULL;
}

/* HashTableDestroy
 * Releases all resources 
 * associated with the hashtable */
void HashTableDestroy(HashTable_t *HashTable)
{
	_CRT_UNUSED(HashTable);
}

/* HashTableInsert
 * Inserts an object with the given
 * string key from the hash table */
void HashTableInsert(HashTable_t *HashTable, DataKey_t Key, void *Data)
{
	_CRT_UNUSED(HashTable);
	_CRT_UNUSED(Key);
	_CRT_UNUSED(Data);
}

/* HashTableRemove 
 * Removes an object with the given 
 * string key from the hash table */
void HashTableRemove(HashTable_t *HashTable, DataKey_t Key)
{
	_CRT_UNUSED(HashTable);
	_CRT_UNUSED(Key);
}

/* HashTableGetValue
 * Retrieves the data associated with
 * a value from the hash table */
void *HashTableGetValue(HashTable_t *HashTable, DataKey_t Key)
{
	_CRT_UNUSED(HashTable);
	_CRT_UNUSED(Key);
	return NULL;
}