/**
 * MollenOS
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Generic Collection Implementation
 *  - Implements Collection and queue functionality
 */

#ifndef _GENERIC_COLLECTION_H_
#define _GENERIC_COLLECTION_H_

#include <os/osdefs.h>
#include <ds/ds.h>

typedef struct CollectionItem {
    DataKey_t              Key;
    bool                   Dynamic;
    void*                  Data;

    struct CollectionItem* Link;
    struct CollectionItem* Prev;
} CollectionItem_t;
typedef struct CollectionItem CollectionIterator_t;

typedef struct Collection {
    SafeMemoryLock_t    SyncObject;
    CollectionItem_t*   Head;
    CollectionItem_t*   Tail;
    atomic_size_t       Length;
    KeyType_t           KeyType;
} Collection_t;

#define COLLECTION_INIT(KeyType)  { { 0 }, NULL, NULL, 0, KeyType }
#define COLLECTION_NODE_INIT(Key) { { Key }, false, NULL, NULL, NULL }

#define foreach(i, Collection) CollectionIterator_t *i; for (i = CollectionBegin(Collection); i != NULL; i = CollectionNext(i))
#define foreach_nolink(i, Collection) CollectionIterator_t *i; for (i = CollectionBegin(Collection); i != NULL; )
#define _foreach(i, Collection) for (i = CollectionBegin(Collection); i != NULL; i = CollectionNext(i))
#define _foreach_nolink(i, Collection) for (i = CollectionBegin(Collection); i != NULL; )

_CODE_BEGIN

/**
 * * CollectionCreate
 * Allocates and contructs a new collection
 */
CRTDECL(Collection_t*,
CollectionCreate(
    _In_ KeyType_t KeyType));
    
/**
 * * CollectionConstruct
 * Constructs a new collection with given configuration 
 */
CRTDECL(void,
CollectionConstruct(
    _In_ Collection_t* Collection,
    _In_ KeyType_t     KeyType));

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

/**
 * CollectionSplice
 * * Returns a spliced node list that can be appended to a new list. The list will
 * * at maximum have the requested count or the length of the list
 * @param Collection [In] The collection that will be spliced.
 * @param Count      [In] The maximum number of list nodes that will be extracted.
 */
CRTDECL(CollectionItem_t*,
CollectionSplice(
    _In_ Collection_t* Collection,
    _In_ int           Count));

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

/**
 * * CollectionInsert 
 * Inserts the node into the front of the Collection. This should be used for sorted
 * Collections, but is available for unsorted Collections aswell
 */
CRTDECL(OsStatus_t,
CollectionInsert(
    _In_ Collection_t*     Collection, 
    _In_ CollectionItem_t* Node));

/**
 * * CollectionAppend
 * Inserts the node into the the back of the Collection. This function is not
 * available for sorted Collections, it will simply redirect to CollectionInsert 
 */
CRTDECL(OsStatus_t,
CollectionAppend(
    _In_ Collection_t*          Collection,
    _In_ CollectionItem_t*      Node));

/**
 * * CollectionPopFront
 * Removes and returns the first element in the collection.
 */
CRTDECL(CollectionItem_t*,
CollectionPopFront(
    _In_ Collection_t*          Collection));

/**
 * * CollectionGetNodeByKey
 * These are the node-retriever functions 
 * they return the Collection-node by either key data or index
 */
CRTDECL(CollectionItem_t*,
CollectionGetNodeByKey(
    _In_ Collection_t* Collection,
    _In_ DataKey_t     Key, 
    _In_ int           n));

/**
 * * CollectionGetDataByKey
 * Finds the n-occurence of an element with the given key and returns
 * the associated data with it
 */
CRTDECL(void*,
CollectionGetDataByKey(
    _In_ Collection_t* Collection, 
    _In_ DataKey_t     Key, 
    _In_ int           n));

/**
 * * CollectionExecuteOnKey
 * These functions execute a given function on all items matching the given key
 */
CRTDECL(void,
CollectionExecuteOnKey(
    _In_ Collection_t* Collection,
    _In_ void          (*Function)(void*, int, void*),
    _In_ DataKey_t     Key,
    _In_ void*         Context));

/**
 * * CollectionExecute(s)
 * These functions execute a given function on all items in the collection
 */
CRTDECL(void,
CollectionExecuteAll(
    _In_ Collection_t* Collection,
    _In_ void          (*Function)(void*, int, void*),
    _In_ void*         Context));

/**
 * * CollectionUnlinkNode
 * This functions unlinks a node and returns the next node for usage
 */
CRTDECL(CollectionItem_t*,
CollectionUnlinkNode(
    _In_ Collection_t*     Collection, 
    _In_ CollectionItem_t* Node));

/**
 * * CollectionRemoveByNode
 * These are the deletion functions and remove based on either node or key 
 */
CRTDECL(OsStatus_t,
CollectionRemoveByNode(
    _In_ Collection_t*     Collection,
    _In_ CollectionItem_t* Node));

/**
 * CollectionRemoveByKey
 * These are the deletion functions and remove based on either node or key */
CRTDECL(OsStatus_t,
CollectionRemoveByKey(
    _In_ Collection_t* Collection, 
    _In_ DataKey_t     Key));

#endif //!_GENERIC_COLLECTION_H_
