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

/* Includes 
 * - Library */
#include <ds/collection.h>
#include <stddef.h>
#include <string.h>

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

/* CollectionClear
 * Clears the Collection of members, cleans up nodes. */
OsStatus_t
CollectionClear(
    _In_ Collection_t *Collection)
{
    // Variables
    CollectionItem_t *Node = NULL;

    // Sanitize parameters
    if (Collection == NULL) {
        return OsError;
    }

    // Get initial node and then
    // just iterate while destroying nodes
    Node = CollectionPopFront(Collection);
    while (Node != NULL) {
        CollectionDestroyNode(Collection, Node);
        Node = CollectionPopFront(Collection);
    }

    // Free the Collection structure and we
    // are done
    return OsSuccess;
}

/* CollectionDestroy
 * Destroys the Collection and frees all resources associated
 * does also free all Collection elements and keys */
OsStatus_t
CollectionDestroy(
    _In_ Collection_t *Collection)
{
    // Variables
    CollectionItem_t *Node = NULL;

    // Sanitize parameters
    if (Collection == NULL) {
        return OsError;
    }

    // Get initial node and then
    // just iterate while destroying nodes
    Node = CollectionPopFront(Collection);
    while (Node != NULL) {
        CollectionDestroyNode(Collection, Node);
        Node = CollectionPopFront(Collection);
    }

    // Free the Collection structure and we
    // are done
    dsfree(Collection);
    return OsSuccess;
}

/* CollectionLength
 * Returns the length of the given Collection */
size_t
CollectionLength(
    _In_ Collection_t *Collection)
{
    // Sanitize the parameters
    if (Collection == NULL) {
        return 0;
    }
    return Collection->Length;
}

/* CollectionBegin
 * Retrieves the starting element of the Collection */
CollectionIterator_t*
CollectionBegin(
    _In_ Collection_t *Collection)
{
    // Sanitize the parameter
    if (Collection == NULL) {
        return NULL;
    }
    else {
        return Collection->Headp;
    }
}

/* CollectionNext
 * Iterates to the next element in the Collection and returns
 * NULL when the end has been reached */
CollectionIterator_t*
CollectionNext(
    _In_ CollectionIterator_t *It)
{
    // Sanitize the parameter
    if (It == NULL) {
        return NULL;
    }
    else {
        return It->Link;
    }
}

/* CollectionCreateNode
 * Instantiates a new Collection node that can be appended to the Collection 
 * by CollectionAppend. If using an unsorted Collection set the sortkey == key */
CollectionItem_t*
CollectionCreateNode(
    _In_ DataKey_t Key,
    _In_ void *Data)
{
    // Allocate a new instance of the Collection-node
    CollectionItem_t *Node = (CollectionItem_t*)dsalloc(sizeof(CollectionItem_t));
    memset(Node, 0, sizeof(CollectionItem_t));

    // Set data
    Node->Key = Key;
    Node->Data = Data;
    return Node;
}

/* CollectionDestroyNode
 * Cleans up a Collection node and frees all resources it had */
OsStatus_t
CollectionDestroyNode(
    _In_ Collection_t *Collection,
    _In_ CollectionItem_t *Node)
{
    // Behave different based on the type of key
    switch (Collection->KeyType) {
        case KeyPointer:
        case KeyString:
            dsfree(Node->Key.Pointer);
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
    _In_ Collection_t *Collection, 
    _In_ CollectionItem_t *Node, 
    _In_ int Position)
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
    _In_ Collection_t *Collection, 
    _In_ CollectionItem_t *Node)
{
    // Sanitize parameters
    if (Collection == NULL || Node == NULL) {
        return OsError;
    }
    
    // In case the Collection is empty - no processing needed
    if (Collection->Headp == NULL || Collection->Tailp == NULL) {
        Collection->Tailp = Collection->Headp = Node;
        Node->Link = NULL;
    }
    else {
        // Make the node point to head
        Node->Link = Collection->Headp;
        Collection->Headp->Prev = Node;
        Collection->Headp = Node;
    }

    // Update previous link to NULL
    Node->Prev = NULL;

    // Collection just got larger!
    Collection->Length++;
    return OsSuccess;
}

/* CollectionAppend
 * Inserts the node into the the back of the Collection. This function is not
 * available for sorted Collections, it will simply redirect to CollectionInsert */
OsStatus_t
CollectionAppend(
    _In_ Collection_t *Collection,
    _In_ CollectionItem_t *Node)
{
    // Sanitize parameters
    if (Collection == NULL || Node == NULL) {
        return OsError;
    }

    // In case of empty Collection just update head/tail
    if (Collection->Headp == NULL || Collection->Tailp == NULL) {
        Node->Prev = NULL;
        Collection->Tailp = Collection->Headp = Node;
    }
    else {
        // Append to tail
        Collection->Tailp->Link = Node;
        Node->Prev = Collection->Tailp;
        Collection->Tailp = Node;
    }

    // Always keep last link NULL
    Node->Link = NULL;
    Collection->Length++;
    return OsSuccess;
}

/* Collection pop functions, the either
 * remove an element from the back or
 * the front of the given Collection and return
 * the node */
CollectionItem_t*
CollectionPopFront(
    _In_ Collection_t *Collection)
{
    // Variables
    CollectionItem_t *Current = NULL;

    // Sanitize parameters
    if (Collection == NULL || Collection->Headp == NULL) {
        return NULL;
    }

    // Manipulate the Collection to find the next pointer of the
    // node that comes before the one to be removed.
    Current = Collection->Headp;

    // Update head pointer
    Collection->Headp = Current->Link;

    // Set previous to null
    if (Collection->Headp != NULL) {
        Collection->Headp->Prev = NULL;
    }

    // Update tail if necessary
    if (Collection->Tailp == Current) {
        Collection->Headp = Collection->Tailp = NULL;
    }

    // Reset its link (remove any Collection traces!)
    Current->Link = NULL;
    Current->Prev = NULL;

    // Update length
    Collection->Length--;
    return Current;
}

/* Collection pop functions, the either
 * remove an element from the back or
 * the front of the given Collection and return
 * the node */
CollectionItem_t*
CollectionPopBack(
    _In_ Collection_t *Collection)
{
    _CRT_UNUSED(Collection);
    return NULL;
}

/* CollectionGetNodeByKey
 * These are the node-retriever functions 
 * they return the Collection-node by either key data or index */
CollectionItem_t*
CollectionGetNodeByKey(
    _In_ Collection_t *Collection,
    _In_ DataKey_t Key, 
    _In_ int n)
{
    // Variables
    CollectionItem_t *It, *Found = NULL;
    int Counter = n;

    // Sanitize parameters
    if (Collection == NULL || Collection->Headp == NULL || Collection->Length == 0) {
        return NULL;
    }

    // Iterate each member in the given Collection and
    // match on the key
    _foreach(It, Collection) {
        if (!dsmatchkey(Collection->KeyType, It->Key, Key)) {
            if (Counter == 0) {
                Found = It;
                break;
            }
            else {
                Counter--;
            }
        }
    }
    return Found;
}

/* These are the data-retriever functions 
 * they return the Collection-node by either key
 * node or index */
void*
CollectionGetDataByKey(
    _In_ Collection_t *Collection, 
    _In_ DataKey_t Key, 
    _In_ int n)
{
    // Reuse our get-node function
    CollectionItem_t *Node = CollectionGetNodeByKey(Collection, Key, n);

    // Sanitize the lookup
    if (Node != NULL) {
        return Node->Data;
    }
    else {
        return NULL;
    }
}

/* These functions execute a given function
 * on all relevant nodes (see names) */
void
CollectionExecuteOnKey(
    _In_ Collection_t *Collection, 
    _In_ void(*Function)(void*, int, void*), 
    _In_ DataKey_t Key, 
    _In_ void *UserData)
{
    // Variables
    CollectionItem_t *Node = NULL;
    int Itr = 0;

    // Sanitize parameters
    if (Collection == NULL || Collection->Headp == NULL || Collection->Length == 0) {
        return;
    }

    // Iterate the Collection and match key
    _foreach(Node, Collection) {
        if (!dsmatchkey(Collection->KeyType, Node->Key, Key)) {
            Function(Node->Data, Itr++, UserData);
        }
    }
}

/* These functions execute a given function
 * on all relevant nodes (see names) */
void
CollectionExecuteAll(
    _In_ Collection_t *Collection, 
    _In_ void(*Function)(void*, int, void*), 
    _In_ void *UserData)
{
    // Variables
    CollectionItem_t *Node = NULL;
    int Itr = 0;

    // Sanitize the paramters
    if (Collection == NULL || Collection->Headp == NULL || Collection->Length == 0) {
        return;
    }

    // Iteate and execute function given
    _foreach(Node, Collection) {
        Function(Node->Data, Itr++, UserData);
    }
}

/* CollectionUnlinkNode
 * This functions unlinks a node and returns the next node for usage */
CollectionItem_t*
CollectionUnlinkNode(
    _In_ Collection_t *Collection, 
    _In_ CollectionItem_t *Node)
{
    // Sanitize parameters
    if (Collection == NULL || Collection->Headp == NULL
        || Node == NULL) {
        return NULL;
    }

    // There are a few cases we need to handle
    // in order for this to be O(1)
    if (Node->Prev == NULL) {
        // Ok, so this means we are the
        // first node in the Collection. Do we have a link?
        if (Node->Link == NULL) {
            // We're the only link
            // but lets stil validate we're from this Collection
            if (Collection->Headp == Node) {
                Collection->Headp = Collection->Tailp = NULL;
                Collection->Length--;
            }
        }
        else {
            // We have a link this means we set headp to next
            if (Collection->Headp == Node) {
                Collection->Headp = Node->Link;
                Collection->Headp->Prev = NULL;
                Collection->Length--;
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
            if (Collection->Tailp == Node) {
                Collection->Tailp = Node->Prev;
                Collection->Tailp->Link = NULL;
                Collection->Length--;
            }
        }
        else {
            // Normal case, we just skip this
            // element without interfering with the Collection
            // pointers
            CollectionItem_t *Prev = Node->Prev;
            Prev->Link = Node->Link;
            Prev->Link->Prev = Prev;
            Collection->Length--;
        }
    }

    // Return the next node
    if (Node->Prev == NULL) {
        return Collection->Headp;
    }
    else {
        return Node->Link;
    }
}

/* CollectionRemove
 * These are the deletion functions 
 * and remove based on either node index or key */
OsStatus_t
CollectionRemoveByNode(
    _In_ Collection_t *Collection,
    _In_ CollectionItem_t* Node)
{
    // Sanitize params
    if (Collection == NULL || Node == NULL) {
        return OsError;
    }

    // Reuse the unlink function and discard
    // the return value
    CollectionUnlinkNode(Collection, Node);

    // Update links
    Node->Link = NULL;
    Node->Prev = NULL;

    // Done - no errors
    return OsSuccess;
}

/* CollectionRemove
 * These are the deletion functions 
 * and remove based on either node index or key */
OsStatus_t
CollectionRemoveByIndex(
    _In_ Collection_t *Collection, 
    _In_ int Index)
{
    _CRT_UNUSED(Collection);
    _CRT_UNUSED(Index);
    return OsSuccess;
}

/* CollectionRemove
 * These are the deletion functions 
 * and remove based on either node index or key */
OsStatus_t
CollectionRemoveByKey(
    _In_ Collection_t *Collection, 
    _In_ DataKey_t Key)
{
    // Variables    
    CollectionItem_t *Node = NULL;

    // Sanitize Collection
    if (Collection == NULL) {
        return OsError;
    }

    // Lookup node
    Node = CollectionGetNodeByKey(Collection, Key, 0);

    // If found, unlink it and destroy it
    if (Node != NULL) {
        if (CollectionRemoveByNode(Collection, Node) != OsSuccess
            || CollectionDestroyNode(Collection, Node) != OsSuccess) {
            return OsError;
        }
        else {
            return OsSuccess;
        }
    }

    // Node wasn't found
    return OsError;
}
