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

#include <os/osdefs.h>
#include <ds/ds.h>

/* CollectionItem
 * Generic collection item that can be indexed by a generic key. */
typedef struct _CollectionItem {
    DataKey_t               Key;
    bool                    Dynamic;
    void*                   Data;

    struct _CollectionItem* Link;
    struct _CollectionItem* Prev;
} CollectionItem_t;
typedef struct _CollectionItem CollectionIterator_t;
#define COLLECTION_NODE_INIT(Node, Key) (Node)->Key.Value = Key.Value; (Node)->Data = 0; (Node)->Link = 0; (Node)->Prev = 0 

/* Collection
 * Generic collection implemented by doubly linked list. */
typedef struct _Collection {
    SafeMemoryLock_t    SyncObject;
    KeyType_t           KeyType;
    atomic_size_t       Length;

    CollectionItem_t*   Head;
    CollectionItem_t*   Tail;
} Collection_t;
#define COLLECTION_INIT(KeyType) { { 0 }, KeyType, 0, NULL, NULL }

/* Foreach Macro(s)
 * They help keeping the code clean and readable when coding loops */
#define foreach(i, Collection) CollectionIterator_t *i; for (i = CollectionBegin(Collection); i != NULL; i = CollectionNext(i))
#define foreach_nolink(i, Collection) CollectionIterator_t *i; for (i = CollectionBegin(Collection); i != NULL; )
#define _foreach(i, Collection) for (i = CollectionBegin(Collection); i != NULL; i = CollectionNext(i))
#define _foreach_nolink(i, Collection) for (i = CollectionBegin(Collection); i != NULL; )

_CODE_BEGIN
/* CollectionCreate
 * Instantiates a new Collection with the given attribs and keytype */
CRTDECL(Collection_t*,
CollectionCreate(
    _In_ KeyType_t              KeyType));
    
/* CollectionConstruct
 * Instantiates a new static Collection with the given attribs and keytype */
CRTDECL(void,
CollectionConstruct(
    _In_ Collection_t*          Collection,
    _In_ KeyType_t              KeyType));

/* CollectionClear
 * Clears the Collection of members, cleans up nodes. */
CRTDECL(OsStatus_t,
CollectionClear(
    _In_ Collection_t*          Collection));

/* CollectionDestroy
 * Destroys the Collection and frees all resources associated
 * does also free all Collection elements and keys */
CRTDECL(OsStatus_t,
CollectionDestroy(
    _In_ Collection_t*          Collection));

/* CollectionLength
 * Returns the length of the given Collection */
CRTDECL(size_t,
CollectionLength(
    _In_ Collection_t*          Collection));

/* CollectionBegin
 * Retrieves the starting element of the Collection */
CRTDECL(CollectionIterator_t*,
CollectionBegin(
    _In_ Collection_t*          Collection));

/* CollectionNext
 * Iterates to the next element in the Collection and returns
 * NULL when the end has been reached */
CRTDECL(CollectionIterator_t*,
CollectionNext(
    _In_ CollectionIterator_t*  It));

/* CollectionCreateNode
 * Instantiates a new Collection node that can be appended to the Collection 
 * by CollectionAppend. If using an unsorted Collection set the sortkey == key */
CRTDECL(CollectionItem_t*,
CollectionCreateNode(
    _In_ DataKey_t              Key,
    _In_ void*                  Data));

/* CollectionDestroyNode
 * Cleans up a Collection node and frees all resources it had */
CRTDECL(OsStatus_t,
CollectionDestroyNode(
    _In_ Collection_t*          Collection,
    _In_ CollectionItem_t*      Node));

/* CollectionInsertAt
 * Insert the node into a specific position in the Collection, if position is invalid it is
 * inserted at the back. This function is not available for sorted Collections, it will simply 
 * call CollectionInsert instead */
CRTDECL(OsStatus_t,
CollectionInsertAt(
    _In_ Collection_t*          Collection, 
    _In_ CollectionItem_t*      Node, 
    _In_ int                    Position));

/* CollectionInsert 
 * Inserts the node into the front of the Collection. This should be used for sorted
 * Collections, but is available for unsorted Collections aswell */
CRTDECL(OsStatus_t,
CollectionInsert(
    _In_ Collection_t*          Collection, 
    _In_ CollectionItem_t*      Node));

/* CollectionAppend
 * Inserts the node into the the back of the Collection. This function is not
 * available for sorted Collections, it will simply redirect to CollectionInsert */
CRTDECL(OsStatus_t,
CollectionAppend(
    _In_ Collection_t*          Collection,
    _In_ CollectionItem_t*      Node));

/* CollectionPopFront
 * Removes and returns the first element in the collection. */
CRTDECL(CollectionItem_t*,
CollectionPopFront(
    _In_ Collection_t*          Collection));

/* CollectionPopBack
 * Removes and returns the last element in the collection. */
CRTDECL(CollectionItem_t*,
CollectionPopBack(
    _In_ Collection_t*          Collection));

/* CollectionGetNodeByKey
 * These are the node-retriever functions 
 * they return the Collection-node by either key data or index */
CRTDECL(CollectionItem_t*,
CollectionGetNodeByKey(
    _In_ Collection_t*          Collection,
    _In_ DataKey_t              Key, 
    _In_ int                    n));

/* CollectionGetDataByKey
 * Finds the n-occurence of an element with the given key and returns
 * the associated data with it */
CRTDECL(void*,
CollectionGetDataByKey(
    _In_ Collection_t*          Collection, 
    _In_ DataKey_t              Key, 
    _In_ int                    n));

/* CollectionExecute(s)
 * These functions execute a given function on all relevant nodes (see names) */
CRTDECL(void,
CollectionExecuteOnKey(
    _In_ Collection_t*          Collection,
    _In_ void                   (*Function)(void*, int, void*),
    _In_ DataKey_t              Key,
    _In_ void*                  UserData));

/* CollectionExecute(s)
 * These functions execute a given function on all relevant nodes (see names) */
CRTDECL(void,
CollectionExecuteAll(
    _In_ Collection_t*          Collection,
    _In_ void                   (*Function)(void*, int, void*),
    _In_ void*                  UserData));

/* CollectionUnlinkNode
 * This functions unlinks a node and returns the next node for usage */
CRTDECL(CollectionItem_t*,
CollectionUnlinkNode(
    _In_ Collection_t*          Collection, 
    _In_ CollectionItem_t*      Node));

/* CollectionRemove
 * These are the deletion functions and remove based on either node index or key */
CRTDECL(OsStatus_t,
CollectionRemoveByNode(
    _In_ Collection_t*          Collection,
    _In_ CollectionItem_t*      Node));

/* CollectionRemove
 * These are the deletion functions and remove based on either node index or key */
CRTDECL(OsStatus_t,
CollectionRemoveByIndex(
    _In_ Collection_t*          Collection, 
    _In_ int                    Index));

/* CollectionRemove
 * These are the deletion functions and remove based on either node index or key */
CRTDECL(OsStatus_t,
CollectionRemoveByKey(
    _In_ Collection_t*          Collection, 
    _In_ DataKey_t              Key));

#endif //!_GENERIC_COLLECTION_H_
