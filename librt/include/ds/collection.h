/* MollenOS
 *
 * Copyright 2011 - 2018, Philip Meulengracht
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
 * MollenOS - Generic Collection Implementation
 *  - Implements Collection and queue functionality
 */

#ifndef _GENERIC_COLLECTION_H_
#define _GENERIC_COLLECTION_H_

/* Includes 
 * - Library */
#include <os/osdefs.h>
#include <ds/ds.h>

/* CollectionItem
 * Generic collection item that can be indexed by a generic key. */
typedef struct _CollectionItem {
    DataKey_t                Key;
    void                    *Data;

    struct _CollectionItem  *Link;
    struct _CollectionItem  *Prev;
} CollectionItem_t;

/* This is the Collection structure
 * it holds basic information about the Collection */
typedef struct _CollectionItem CollectionIterator_t;
typedef struct _Collection Collection_t;

/* Foreach Macro(s)
 * They help keeping the code clean and readable when coding loops */
#define foreach(i, Collection) CollectionItem_t *i; for (i = CollectionBegin(Collection); i != NULL; i = CollectionNext(i))
#define foreach_nolink(i, Collection) CollectionItem_t *i; for (i = CollectionBegin(Collection); i != NULL; )
#define _foreach(i, Collection) for (i = CollectionBegin(Collection); i != NULL; i = CollectionNext(i))
#define _foreach_nolink(i, Collection) for (i = CollectionBegin(Collection); i != NULL; )

/* Protect against c++ files */
_CODE_BEGIN

/* CollectionCreate
 * Instantiates a new Collection with the given attribs and keytype */
MOSAPI
Collection_t*
MOSABI
CollectionCreate(
    _In_ KeyType_t KeyType);
    
/* CollectionClear
 * Clears the Collection of members, cleans up nodes. */
MOSAPI
OsStatus_t
MOSABI
CollectionClear(
    _In_ Collection_t *Collection);

/* CollectionDestroy
 * Destroys the Collection and frees all resources associated
 * does also free all Collection elements and keys */
MOSAPI
OsStatus_t
MOSABI
CollectionDestroy(
    _In_ Collection_t *Collection);

/* CollectionLength
 * Returns the length of the given Collection */
MOSAPI
size_t
MOSABI
CollectionLength(
    _In_ Collection_t *Collection);

/* CollectionBegin
 * Retrieves the starting element of the Collection */
MOSAPI
CollectionIterator_t*
MOSABI
CollectionBegin(
    _In_ Collection_t *Collection);

/* CollectionNext
 * Iterates to the next element in the Collection and returns
 * NULL when the end has been reached */
MOSAPI
CollectionIterator_t*
MOSABI
CollectionNext(
    _In_ CollectionIterator_t *It);

/* CollectionCreateNode
 * Instantiates a new Collection node that can be appended to the Collection 
 * by CollectionAppend. If using an unsorted Collection set the sortkey == key */
MOSAPI
CollectionItem_t*
MOSABI
CollectionCreateNode(
    _In_ DataKey_t Key,
    _In_ void *Data);

/* CollectionDestroyNode
 * Cleans up a Collection node and frees all resources it had */
MOSAPI
OsStatus_t
MOSABI
CollectionDestroyNode(
    _In_ Collection_t *Collection,
    _In_ CollectionItem_t *Node);

/* CollectionInsertAt
 * Insert the node into a specific position in the Collection, if position is invalid it is
 * inserted at the back. This function is not available for sorted Collections, it will simply 
 * call CollectionInsert instead */
MOSAPI
OsStatus_t
MOSABI
CollectionInsertAt(
    _In_ Collection_t *Collection, 
    _In_ CollectionItem_t *Node, 
    _In_ int Position);

/* CollectionInsert 
 * Inserts the node into the front of the Collection. This should be used for sorted
 * Collections, but is available for unsorted Collections aswell */
MOSAPI
OsStatus_t
MOSABI
CollectionInsert(
    _In_ Collection_t *Collection, 
    _In_ CollectionItem_t *Node);

/* CollectionAppend
 * Inserts the node into the the back of the Collection. This function is not
 * available for sorted Collections, it will simply redirect to CollectionInsert */
MOSAPI
OsStatus_t
MOSABI
CollectionAppend(
    _In_ Collection_t *Collection,
    _In_ CollectionItem_t *Node);

/* Collection pop functions, the either 
 * remove an element from the back or 
 * the front of the given Collection and return the node */
MOSAPI
CollectionItem_t*
MOSABI
CollectionPopFront(
    _In_ Collection_t *Collection);

MOSAPI
CollectionItem_t*
MOSABI
CollectionPopBack(
    _In_ Collection_t *Collection);

/* CollectionGetNodeByKey
 * These are the node-retriever functions 
 * they return the Collection-node by either key data or index */
MOSAPI
CollectionItem_t*
MOSABI
CollectionGetNodeByKey(
    _In_ Collection_t *Collection,
    _In_ DataKey_t Key, 
    _In_ int n);

/* These are the data-retriever functions 
 * they return the Collection-node by either key
 * node or index */
MOSAPI
void*
MOSABI
CollectionGetDataByKey(
    _In_ Collection_t *Collection, 
    _In_ DataKey_t Key, 
    _In_ int n);

/* CollectionExecute(s)
 * These functions execute a given function on all relevant nodes (see names) */
MOSAPI
void
MOSABI
CollectionExecuteOnKey(
    _In_ Collection_t *Collection,
    _In_ void(*Function)(void*, int, void*),
    _In_ DataKey_t Key,
    _In_ void *UserData);

/* CollectionExecute(s)
 * These functions execute a given function on all relevant nodes (see names) */
MOSAPI
void
MOSABI
CollectionExecuteAll(
    _In_ Collection_t *Collection,
    _In_ void(*Function)(void*, int, void*),
    _In_ void *UserData);

/* CollectionUnlinkNode
 * This functions unlinks a node and returns the next node for usage */
MOSAPI
CollectionItem_t*
MOSABI
CollectionUnlinkNode(
    _In_ Collection_t *Collection, 
    _In_ CollectionItem_t *Node);

/* CollectionRemove
 * These are the deletion functions 
 * and remove based on either node index or key */
MOSAPI
OsStatus_t
MOSABI
CollectionRemoveByNode(
    _In_ Collection_t *Collection,
    _In_ CollectionItem_t* Node);

/* CollectionRemove
 * These are the deletion functions 
 * and remove based on either node index or key */
MOSAPI
OsStatus_t
MOSABI
CollectionRemoveByIndex(
    _In_ Collection_t *Collection, 
    _In_ int Index);

/* CollectionRemove
 * These are the deletion functions 
 * and remove based on either node index or key */
MOSAPI
OsStatus_t
MOSABI
CollectionRemoveByKey(
    _In_ Collection_t *Collection, 
    _In_ DataKey_t Key);

#endif //!_GENERIC_COLLECTION_H_
