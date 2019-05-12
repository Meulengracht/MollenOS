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

#include <ds/collection.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>

/* CollectionCreate
 * Instantiates a new collection with the specified key-type. */
Collection_t*
CollectionCreate(
    _In_ KeyType_t KeyType)
{
    // Allocate a new Collection structure
    Collection_t *Collection = (Collection_t*)dsalloc(sizeof(Collection_t));
    memset(Collection, 0, sizeof(Collection_t));

    // Set initial information
    Collection->KeyType = KeyType;
    return Collection;
}

/* CollectionConstruct
 * Instantiates a new static Collection with the given attribs and keytype */
void
CollectionConstruct(
    _In_ Collection_t* Collection,
    _In_ KeyType_t     KeyType)
{
    memset(Collection, 0, sizeof(Collection_t));
    Collection->KeyType = KeyType;
}

/* CollectionClear
 * Clears the Collection of members, cleans up nodes. */
OsStatus_t
CollectionClear(
    _In_ Collection_t*          Collection)
{
    CollectionItem_t *Node = NULL;
    assert(Collection != NULL);

    // Get initial node and then
    // just iterate while destroying nodes
    Node = CollectionPopFront(Collection);
    while (Node != NULL) {
        CollectionDestroyNode(Collection, Node);
        Node = CollectionPopFront(Collection);
    }
    return OsSuccess;
}

OsStatus_t
CollectionDestroy(
    _In_ Collection_t* Collection)
{
    CollectionItem_t *Node = NULL;
    assert(Collection != NULL);

    // Get initial node and then
    // just iterate while destroying nodes
    Node = CollectionPopFront(Collection);
    while (Node != NULL) {
        CollectionDestroyNode(Collection, Node);
        Node = CollectionPopFront(Collection);
    }
    dsfree(Collection);
    return OsSuccess;
}

size_t
CollectionLength(
    _In_ Collection_t* Collection)
{
    assert(Collection != NULL);
    return atomic_load(&Collection->Length);
}

CollectionIterator_t*
CollectionBegin(
    _In_ Collection_t* Collection)
{
    assert(Collection != NULL);
    return Collection->Head;
}

CollectionIterator_t*
CollectionNext(
    _In_ CollectionIterator_t* It)
{
    return (It == NULL) ? NULL : It->Link;
}

/* CollectionCreateNode
 * Instantiates a new Collection node that can be appended to the Collection 
 * by CollectionAppend. If using an unsorted Collection set the sortkey == key */
CollectionItem_t*
CollectionCreateNode(
    _In_ DataKey_t              Key,
    _In_ void*                  Data)
{
    // Allocate a new instance of the Collection-node
    CollectionItem_t *Node = (CollectionItem_t*)dsalloc(sizeof(CollectionItem_t));
    memset(Node, 0, sizeof(CollectionItem_t));
    Node->Key       = Key;
    Node->Data      = Data;
    Node->Dynamic   = true;
    return Node;
}

/* CollectionDestroyNode
 * Cleans up a Collection node and frees all resources it had */
OsStatus_t
CollectionDestroyNode(
    _In_ Collection_t*          Collection,
    _In_ CollectionItem_t*      Node)
{
    assert(Collection != NULL);
    assert(Node != NULL);
    if (Node->Dynamic == false) {
        return OsSuccess;
    }

    // Behave different based on the type of key
    switch (Collection->KeyType) {
        case KeyString:
            dsfree((void*)Node->Key.Value.String.Pointer);
            break;

        default:
            break;
    }

    // Cleanup node and return
    dsfree(Node);
    return OsSuccess;
}

/* CollectionInsertAt
 * Insert the node into a specific position in the Collection, if position is invalid it is
 * inserted at the back. This function is not available for sorted Collections, it will simply 
 * call CollectionInsert instead */
OsStatus_t
CollectionInsertAt(
    _In_ Collection_t*          Collection, 
    _In_ CollectionItem_t*      Node, 
    _In_ int                    Position)
{
    // Sanitize parameters
    if (Collection == NULL || Node == NULL) {
        return OsError;
    }

    // We need to make this implementation
    _CRT_UNUSED(Position);

    // todo
    return OsSuccess;
}

/* CollectionInsert 
 * Inserts the node into the front of the Collection. This should be used for sorted
 * Collections, but is available for unsorted Collections aswell */
OsStatus_t
CollectionInsert(
    _In_ Collection_t*          Collection, 
    _In_ CollectionItem_t*      Node)
{
    assert(Collection != NULL);
    assert(Node != NULL);

    // Set previous
    Node->Prev = NULL;

    // In case the Collection is empty - no processing needed
    dslock(&Collection->SyncObject);
    if (Collection->Head == NULL || Collection->Tail == NULL) {
        Node->Link          = NULL;
        Collection->Tail    = Node;
        Collection->Head    = Node;
    }
    else {
        // Make the node point to head
        Node->Link              = Collection->Head;
        Collection->Head->Prev  = Node;
        Collection->Head        = Node;
    }
    atomic_fetch_add(&Collection->Length, 1);
    dsunlock(&Collection->SyncObject);
    return OsSuccess;
}

/* CollectionAppend
 * Inserts the node into the the back of the Collection. This function is not
 * available for sorted Collections, it will simply redirect to CollectionInsert */
OsStatus_t
CollectionAppend(
    _In_ Collection_t*          Collection,
    _In_ CollectionItem_t*      Node)
{
    assert(Collection != NULL);
    assert(Node != NULL);

    // Set eol
    Node->Link = NULL;

    // In case of empty Collection just update head/tail
    dslock(&Collection->SyncObject);
    if (Collection->Head == NULL || Collection->Tail == NULL) {
        Node->Prev          = NULL;
        Collection->Tail    = Node;
        Collection->Head    = Node;
    }
    else {
        // Append to tail
        Node->Prev              = Collection->Tail;
        Collection->Tail->Link  = Node;
        Collection->Tail        = Node;
    }
    atomic_fetch_add(&Collection->Length, 1);
    dsunlock(&Collection->SyncObject);
    return OsSuccess;
}

/* CollectionPopFront
 * Removes and returns the first element in the collection. */
CollectionItem_t*
CollectionPopFront(
    _In_ Collection_t*          Collection)
{
    // Variables
    CollectionItem_t *Current = NULL;

    // Do some sanity checks on the state of the collection
    assert(Collection != NULL);
    if (Collection->Head == NULL) {
        return NULL;
    }

    // Manipulate the Collection to find the next pointer of the
    // node that comes before the one to be removed.
    dslock(&Collection->SyncObject);
    Current             = Collection->Head;
    Collection->Head    = Current->Link;

    // Set previous to null
    if (Collection->Head != NULL) {
        Collection->Head->Prev = NULL;
    }

    // Update tail if necessary
    if (Collection->Tail == Current) {
        Collection->Head = Collection->Tail = NULL;
    }
    atomic_fetch_sub(&Collection->Length, 1);
    dsunlock(&Collection->SyncObject);

    // Reset its link (remove any Collection traces!)
    Current->Link = NULL;
    Current->Prev = NULL;
    return Current;
}

/* CollectionPopBack
 * Removes and returns the last element in the collection. */
CollectionItem_t*
CollectionPopBack(
    _In_ Collection_t*          Collection)
{
    _CRT_UNUSED(Collection);
    return NULL;
}

/* CollectionGetNodeByKey
 * These are the node-retriever functions 
 * they return the Collection-node by either key data or index */
CollectionItem_t*
CollectionGetNodeByKey(
    _In_ Collection_t*          Collection,
    _In_ DataKey_t              Key, 
    _In_ int                    n)
{
    // Variables
    CollectionItem_t *i     = NULL;
    int Counter             = n;

    // Do some sanity checks on the state of the collection
    assert(Collection != NULL);
    if (Collection->Head == NULL) {
        return NULL;
    }

    // Iterate each member in the given Collection and
    // match on the key
    _foreach(i, Collection) {
        if (!dsmatchkey(Collection->KeyType, i->Key, Key)) {
            if (Counter == 0) {
                break;
            }
            Counter--;
        }
    }
    return Counter == 0 ? i : NULL;
}

/* CollectionGetDataByKey
 * Finds the n-occurence of an element with the given key and returns
 * the associated data with it */
void*
CollectionGetDataByKey(
    _In_ Collection_t*          Collection, 
    _In_ DataKey_t              Key, 
    _In_ int                    n)
{
    CollectionItem_t *Node = CollectionGetNodeByKey(Collection, Key, n);
    return (Node == NULL) ? NULL : Node->Data;
}

/* CollectionExecute(s)
 * These functions execute a given function on all relevant nodes (see names) */
void
CollectionExecuteOnKey(
    _In_ Collection_t*          Collection, 
    _In_ void                   (*Function)(void*, int, void*), 
    _In_ DataKey_t              Key, 
    _In_ void*                  UserData)
{
    // Variables
    CollectionItem_t *Node  = NULL;
    int i                   = 0;
    assert(Collection != NULL);

    // Iterate the Collection and match key
    _foreach(Node, Collection) {
        if (!dsmatchkey(Collection->KeyType, Node->Key, Key)) {
            Function(Node->Data, i++, UserData);
        }
    }
}

/* CollectionExecute(s)
 * These functions execute a given function on all relevant nodes (see names) */
void
CollectionExecuteAll(
    _In_ Collection_t*          Collection, 
    _In_ void                   (*Function)(void*, int, void*), 
    _In_ void*                  UserData)
{
    // Variables
    CollectionItem_t *Node  = NULL;
    int i                   = 0;
    assert(Collection != NULL);

    // Iteate and execute function given
    _foreach(Node, Collection) {
        Function(Node->Data, i++, UserData);
    }
}

static void
__collection_remove_node(
    _In_ Collection_t*     Collection, 
    _In_ CollectionItem_t* Node)
{
    if (Node->Prev == NULL) {
        // Ok, so this means we are the
        // first node in the Collection. Do we have a link?
        if (Node->Link == NULL) {
            // We're the only link
            // but lets stil validate we're from this Collection
            if (Collection->Head == Node) {
                Collection->Head = Collection->Tail = NULL;
                atomic_fetch_sub(&Collection->Length, 1);
            }
        }
        else {
            // We have a link this means we set headp to next
            if (Collection->Head == Node) {
                Collection->Head = Node->Link;
                Collection->Head->Prev = NULL;
                atomic_fetch_sub(&Collection->Length, 1);
            }
        }
    }
    else {
        // We have a previous,
        // Special case 1: we are last element
        // which means we should update pointer
        if (Node->Link == NULL) {
            // Ok, we are last element 
            // Update tail pointer to previous
            if (Collection->Tail == Node) {
                Collection->Tail = Node->Prev;
                Collection->Tail->Link = NULL;
                atomic_fetch_sub(&Collection->Length, 1);
            }
        }
        else {
            // Normal case, we just skip this
            // element without interfering with the Collection
            // pointers
            CollectionItem_t *Prev = Node->Prev;
            Prev->Link = Node->Link;
            Prev->Link->Prev = Prev;
            atomic_fetch_sub(&Collection->Length, 1);
        }
    }
}

/* CollectionUnlinkNode
 * This functions unlinks a node and returns the next node for usage */
CollectionItem_t*
CollectionUnlinkNode(
    _In_ Collection_t*          Collection, 
    _In_ CollectionItem_t*      Node)
{
    assert(Collection != NULL);
    assert(Node != NULL);

    // There are a few cases we need to handle
    // in order for this to be O(1)
    dslock(&Collection->SyncObject);
    __collection_remove_node(Collection, Node);
    dsunlock(&Collection->SyncObject);
    return (Node->Prev == NULL) ? Collection->Head : Node->Link;
}

/* CollectionRemove
 * These are the deletion functions and remove based on either node index or key */
OsStatus_t
CollectionRemoveByNode(
    _In_ Collection_t*     Collection,
    _In_ CollectionItem_t* Node)
{
    OsStatus_t Status = OsSuccess;
    
    assert(Collection != NULL);
    assert(Node != NULL);
    
    // Protect against double unlinking
    dslock(&Collection->SyncObject);
    if (Node->Link == NULL) {
        // Then the node should be the end of the list
        if (Collection->Tail != Node) {
            Status = OsDoesNotExist;
        }
    }
    else if (Node->Prev == NULL) {
        // Then the node should be the initial
        if (Collection->Head != Node) {
            Status = OsDoesNotExist;
        }
    }
    
    if (Status != OsDoesNotExist) {
        __collection_remove_node(Collection, Node);
        Node->Link = NULL;
        Node->Prev = NULL;
    }
    dsunlock(&Collection->SyncObject);
    return Status;
}

/* CollectionRemove
 * These are the deletion functions and remove based on either node index or key */
OsStatus_t
CollectionRemoveByIndex(
    _In_ Collection_t*          Collection, 
    _In_ int                    Index)
{
    _CRT_UNUSED(Collection);
    _CRT_UNUSED(Index);
    return OsSuccess;
}

/* CollectionRemove
 * These are the deletion functions and remove based on either node index or key */
OsStatus_t
CollectionRemoveByKey(
    _In_ Collection_t*          Collection, 
    _In_ DataKey_t              Key)
{
    // Variables    
    CollectionItem_t *Node = NULL;
    assert(Collection != NULL);

    // Lookup node
    Node = CollectionGetNodeByKey(Collection, Key, 0);
    if (Node != NULL) {
        if (CollectionRemoveByNode(Collection, Node) != OsSuccess
            || CollectionDestroyNode(Collection, Node) != OsSuccess) {
            return OsError;
        }
        else {
            return OsSuccess;
        }
    }
    return OsError;
}
