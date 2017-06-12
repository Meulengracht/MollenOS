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
 * MollenOS - Generic List
 * Insertion, Deletion runs at O(1) 
 * Lookup by key, or search runs at O(n)
 * Pop, push runs at O(1)
 */

#ifndef _GENERIC_LIST_H_
#define _GENERIC_LIST_H_

/* Includes 
 * - Library */
#include <os/osdefs.h>
#include <ds/ds.h>

/* The list node structure 
 * this is a generic list item
 * that holds an ident (key) and data */
typedef struct _ListNode {
	DataKey_t				 Key;
	DataKey_t				 SortKey;
	void					*Data;

	struct _ListNode		*Link;
	struct _ListNode		*Prev;
} ListNode_t;

/* This is the list structure
 * it holds basic information about the list */
typedef struct _ListNode ListIterator_t;
typedef struct _List List_t;

/* List Definitions 
 * Used to implement list with basic locking mechanisms */
#define LIST_NORMAL				0x0
#define LIST_SAFE				0x1

/* Sorted list? Normally 
 * the list is unsorted but supports different sorts */
#define LIST_SORT_ONINSERT		0x2
#define LIST_SORT_ONCALL		(0x4 | LIST_SORT_ONINSERT)

/* Foreach Macro(s)
 * They help keeping the code clean and readable when coding loops */
#define foreach(i, List) ListNode_t *i; for (i = ListBegin(List); i != NULL; i = ListNext(i))
#define foreach_nolink(i, List) ListNode_t *i; for (i = ListBegin(List); i != NULL; )
#define _foreach(i, List) for (i = ListBegin(List); i != NULL; i = ListNext(i))
#define _foreach_nolink(i, List) for (i = ListBegin(List); i != NULL; )

/* Protect against c++ files */
_CODE_BEGIN

/* ListCreate
 * Instantiates a new list with the given attribs and keytype */
MOSAPI
List_t*
MOSABI
ListCreate(
	_In_ KeyType_t KeyType, 
	_In_ Flags_t Attributes);

/* ListDestroy
 * Destroys the list and frees all resources associated
 * does also free all list elements and keys */
MOSAPI
OsStatus_t
MOSABI
ListDestroy(
	_In_ List_t *List);

/* ListLength
 * Returns the length of the given list */
MOSAPI
size_t
MOSABI
ListLength(
	_In_ List_t *List);

/* ListBegin
 * Retrieves the starting element of the list */
MOSAPI
ListIterator_t*
MOSABI
ListBegin(
	_In_ List_t *List);

/* ListNext
 * Iterates to the next element in the list and returns
 * NULL when the end has been reached */
MOSAPI
ListIterator_t*
MOSABI
ListNext(
	_In_ ListIterator_t *It);

/* ListCreateNode
 * Instantiates a new list node that can be appended to the list 
 * by ListAppend. If using an unsorted list set the sortkey == key */
MOSAPI
ListNode_t*
MOSABI
ListCreateNode(
	_In_ DataKey_t Key, 
	_In_ DataKey_t SortKey, 
	_In_ void *Data);

/* ListDestroyNode
 * Cleans up a list node and frees all resources it had */
MOSAPI
OsStatus_t
MOSABI
ListDestroyNode(
	_In_ List_t *List,
	_In_ ListNode_t *Node);

/* ListInsertAt
 * Insert the node into a specific position in the list, if position is invalid it is
 * inserted at the back. This function is not available for sorted lists, it will simply 
 * call ListInsert instead */
MOSAPI
OsStatus_t
MOSABI
ListInsertAt(
	_In_ List_t *List, 
	_In_ ListNode_t *Node, 
	_In_ int Position);

/* ListInsert 
 * Inserts the node into the front of the list. This should be used for sorted
 * lists, but is available for unsorted lists aswell */
MOSAPI
OsStatus_t
MOSABI
ListInsert(
	_In_ List_t *List, 
	_In_ ListNode_t *Node);

/* ListAppend
 * Inserts the node into the the back of the list. This function is not
 * available for sorted lists, it will simply redirect to ListInsert */
MOSAPI
OsStatus_t
MOSABI
ListAppend(
	_In_ List_t *List,
	_In_ ListNode_t *Node);

/* List pop functions, the either 
 * remove an element from the back or 
 * the front of the given list and return the node */
MOSAPI
ListNode_t*
MOSABI
ListPopFront(
	_In_ List_t *List);

MOSAPI
ListNode_t*
MOSABI
ListPopBack(
	_In_ List_t *List);

/* ListGetNodeByKey
 * These are the node-retriever functions 
 * they return the list-node by either key data or index */
MOSAPI
ListNode_t*
MOSABI
ListGetNodeByKey(
	_In_ List_t *List,
	_In_ DataKey_t Key, 
	_In_ int n);

/* These are the data-retriever functions 
 * they return the list-node by either key
 * node or index */
MOSAPI
void*
MOSABI
ListGetDataByKey(
	_In_ List_t *List, 
	_In_ DataKey_t Key, 
	_In_ int n);

/* ListExecute(s)
 * These functions execute a given function on all relevant nodes (see names) */
MOSAPI
void
MOSABI
ListExecuteOnKey(
	_In_ List_t *List,
	_In_ void(*Function)(void*, int, void*),
	_In_ DataKey_t Key,
	_In_ void *UserData);

/* ListExecute(s)
 * These functions execute a given function on all relevant nodes (see names) */
MOSAPI
void
MOSABI
ListExecuteAll(
	_In_ List_t *List,
	_In_ void(*Function)(void*, int, void*),
	_In_ void *UserData);

/* ListUnlinkNode
 * This functions unlinks a node and returns the next node for usage */
MOSAPI
ListNode_t*
MOSABI
ListUnlinkNode(
	_In_ List_t *List, 
	_In_ ListNode_t *Node);

/* ListRemove
 * These are the deletion functions 
 * and remove based on either node index or key */
MOSAPI
OsStatus_t
MOSABI
ListRemoveByNode(
	_In_ List_t *List,
	_In_ ListNode_t* Node);

/* ListRemove
 * These are the deletion functions 
 * and remove based on either node index or key */
MOSAPI
OsStatus_t
MOSABI
ListRemoveByIndex(
	_In_ List_t *List, 
	_In_ int Index);

/* ListRemove
 * These are the deletion functions 
 * and remove based on either node index or key */
MOSAPI
OsStatus_t
MOSABI
ListRemoveByKey(
	_In_ List_t *List, 
	_In_ DataKey_t Key);

#endif //!_GENERIC_LIST_H_
