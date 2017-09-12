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

#ifndef _GENERIC_HASHTABLE_H_
#define _GENERIC_HASHTABLE_H_

/* Includes 
 * - Library */
#include <os/osdefs.h>
#include <ds/ds.h>
#include <ds/list.h>

/* The hashtable data 
 * structure, this keeps track
 * of keys, size, etc */
typedef struct _HashTable {
	size_t 		  Capacity;
	size_t 		  Size;
	List_t 		**Array;
} HashTable_t;

/* HashTableCreate
 * Initializes a new hash table
 * of the given capacity */
MOSAPI
HashTable_t*
MOSABI
HashTableCreate(
	_In_ KeyType_t KeyType, 
	_In_ size_t Capacity);

/* HashTableDestroy
 * Releases all resources 
 * associated with the hashtable */
MOSAPI
void
MOSABI
HashTableDestroy(
	_In_ HashTable_t *HashTable);

/* HashTableInsert
 * Inserts an object with the given
 * string key from the hash table */
MOSAPI
void
MOSABI
HashTableInsert(
	_In_ HashTable_t *HashTable, 
	_In_ DataKey_t Key, 
	_In_ void *Data);

/* HashTableRemove 
 * Removes an object with the given 
 * string key from the hash table */
MOSAPI
void
MOSABI
HashTableRemove(
	_In_ HashTable_t *HashTable, 
	_In_ DataKey_t Key);

/* HashTableGetValue
 * Retrieves the data associated with
 * a value from the hash table */
MOSAPI
void*
MOSABI
HashTableGetValue(
	_In_ HashTable_t *HashTable, 
	_In_ DataKey_t Key);

#endif //!_HASHTABLE_H_
