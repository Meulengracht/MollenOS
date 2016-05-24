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
* MollenOS - Generic List
* Insertion, Deletion runs at O(1) 
* Lookup by key, or search runs at O(n)
* Pop, push runs at O(1)
*/

#ifndef _GENERIC_LIST_H_
#define _GENERIC_LIST_H_

/* Includes */
#include <crtdefs.h>
#include <stdint.h>
#include <ds/ds.h>

/*******************************
 *    Data Structures          *
 *******************************/

/* The list node structure 
 * this is a generic list item
 * that holds an ident (key)
 * and data */
typedef struct _ListNode
{
	/* Key */
	DataKey_t Key;

	/* Payload */
	void *Data;

	/* Link(s) */
	struct _ListNode *Link;
	struct _ListNode *Prev;

} ListNode_t;

/* This is the list structure
 * it holds basic information
 * about the list */
typedef struct _List
{
	/* Key type */
	KeyType_t KeyType;

	/* Head and Tail */
	ListNode_t *Headp, *Tailp;

	/* Attributes */
	int Attributes;

	/* Length */
	int Length;

	/* Perhaps we use a lock */
	Spinlock_t Lock;

} List_t;

/* List Definitions */
#define LIST_NORMAL		0x0
#define LIST_SAFE		0x1

/* Foreach Macro */
#define foreach(i, List) ListNode_t *i; for (i = List->Headp; i != NULL; i = i->Link)
#define _foreach(i, List) for (i = List->Headp; i != NULL; i = i->Link)

/*******************************
 *         Prototypes          *
 *******************************/

/* Instantiates a new list 
 * with the given attribs and keytype */
_CRT_EXTERN List_t *ListCreate(KeyType_t KeyType, int Attributes);

/* Destroys the list and 
 * frees all resources associated
 * does also free all list elements
 * and keys */
_CRT_EXTERN void ListDestroy(List_t *List);

/* Returns the length of the 
 * given list */
_CRT_EXTERN int ListLength(List_t *List);

/* Instantiates a new list node 
 * that can be appended to the list 
 * by ListAppend */
_CRT_EXTERN ListNode_t *ListCreateNode(DataKey_t Key, void *Data);

/* Cleans up a list node and frees 
 * all resources it had */
_CRT_EXTERN void ListDestroyNode(List_t *List, ListNode_t *Node);

/* Insert the node into a specific position
 * in the list, if position is invalid it is
 * inserted at the back */
_CRT_EXTERN void ListInsert(List_t *List, ListNode_t *Node, int Position);

/* Inserts the node into the front of 
 * the list */
_CRT_EXTERN void ListInsertFront(List_t *List, ListNode_t *Node);

/* Inserts the node into the the back
 * of the list */
_CRT_EXTERN void ListAppend(List_t *List, ListNode_t *Node);

/* List pop functions, the either 
 * remove an element from the back or 
 * the front of the given list and return
 * the node */
_CRT_EXTERN ListNode_t *ListPopFront(List_t *List);
_CRT_EXTERN ListNode_t *ListPopBack(List_t *List);

/* These are the index-retriever functions 
 * they return the given index by either 
 * Key, data or node, return -1 if not found */
_CRT_EXTERN int ListGetIndexByData(List_t *List, void *Data);
_CRT_EXTERN int ListGetIndexByKey(List_t *List, DataKey_t Key);
_CRT_EXTERN int ListGetIndexByNode(List_t *List, ListNode_t *Node);

/* These are the node-retriever functions 
 * they return the list-node by either key
 * data or index */
_CRT_EXTERN ListNode_t *ListGetNodeByKey(List_t *List, DataKey_t Key, int n);

/* These are the data-retriever functions 
 * they return the list-node by either key
 * node or index */
_CRT_EXTERN void *ListGetDataByKey(List_t *List, DataKey_t Key, int n);

/* These functions execute a given function
 * on all relevant nodes (see names) */
_CRT_EXTERN void ListExecuteOnKey(List_t *List, void(*Function)(void*, int, void*), DataKey_t Key, void *UserData);
_CRT_EXTERN void ListExecuteAll(List_t *List, void(*Function)(void*, int, void*), void *UserData);

/* These are the deletion functions 
 * and remove based on either node 
 * index or key */
_CRT_EXTERN void ListRemoveByNode(List_t *List, ListNode_t* Node);
_CRT_EXTERN void ListRemoveByIndex(List_t *List, int Index);
_CRT_EXTERN void ListRemoveByKey(List_t *List, DataKey_t Key);

#endif //!_GENERIC_LIST_H_