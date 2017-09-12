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

/* Includes 
 * - Library */
#include <ds/list.h>
#include <stddef.h>
#include <string.h>

/* Data structures 
 * These are to keep things transparent, and rather keep
 * functions to do stuff than using them directly */
typedef struct _List {
	KeyType_t				 KeyType;
	Spinlock_t				 Lock;
	Flags_t					 Attributes;
	size_t					 Length;

	ListNode_t				*Headp;
	ListNode_t				*Tailp;
} List_t;

/* ListCreate
 * Instantiates a new list with the given attribs and keytype */
List_t*
ListCreate(
	_In_ KeyType_t KeyType, 
	_In_ Flags_t Attributes)
{
	// Allocate a new list structure
	List_t *List = (List_t*)dsalloc(sizeof(List_t));
	memset(List, 0, sizeof(List_t));

	// Set initial information
	List->Attributes = Attributes;
	List->KeyType = KeyType;

	// Take care of attributes
	if (Attributes & LIST_SAFE) {
		SpinlockReset(&List->Lock);
	}
	
	// Done - no errors
	return List;
}

/* ListDestroy
 * Destroys the list and frees all resources associated
 * does also free all list elements and keys */
OsStatus_t
ListDestroy(
	_In_ List_t *List)
{
	// Variables
	ListNode_t *Node = NULL;

	// Sanitize parameters
	if (List == NULL) {
		return OsError;
	}

	// Get initial node and then
	// just iterate while destroying nodes
	Node = ListPopFront(List);
	while (Node != NULL) {
		ListDestroyNode(List, Node);
		Node = ListPopFront(List);
	}

	// Free the list structure and we
	// are done
	dsfree(List);
	return OsSuccess;
}

/* ListLength
 * Returns the length of the given list */
size_t
ListLength(
	_In_ List_t *List)
{
	// Variables
	size_t Size = 0;
	
	// Sanitize the parameters
	if (List == NULL) {
		return 0;
	}

	// Acquire lock
	if (List->Attributes & LIST_SAFE) {
		SpinlockAcquire(&List->Lock);
	}

	// Store in temporary var while we have lock
	Size = List->Length;

	// Release the lock
	if (List->Attributes & LIST_SAFE) {
		SpinlockRelease(&List->Lock);
	}

	// Done
	return Size;
}

/* ListBegin
 * Retrieves the starting element of the list */
ListIterator_t*
ListBegin(
	_In_ List_t *List)
{
	// Sanitize the parameter
	if (List == NULL) {
		return NULL;
	}
	else {
		return List->Headp;
	}
}

/* ListNext
 * Iterates to the next element in the list and returns
 * NULL when the end has been reached */
ListIterator_t*
ListNext(
	_In_ ListIterator_t *It)
{
	// Sanitize the parameter
	if (It == NULL) {
		return NULL;
	}
	else {
		return It->Link;
	}
}

/* ListCreateNode
 * Instantiates a new list node that can be appended to the list 
 * by ListAppend. If using an unsorted list set the sortkey == key */
ListNode_t*
ListCreateNode(
	_In_ DataKey_t Key, 
	_In_ DataKey_t SortKey, 
	_In_ void *Data)
{
	// Allocate a new instance of the list-node
	ListNode_t *Node = (ListNode_t*)dsalloc(sizeof(ListNode_t));
	memset(Node, 0, sizeof(ListNode_t));

	// Set data
	Node->Key = Key;
	Node->SortKey = SortKey;
	Node->Data = Data;
	return Node;
}

/* ListDestroyNode
 * Cleans up a list node and frees all resources it had */
OsStatus_t
ListDestroyNode(
	_In_ List_t *List,
	_In_ ListNode_t *Node)
{
	// Behave different based on the type of key
	switch (List->KeyType) {
		case KeyPointer:
		case KeyString:
			dsfree(Node->Key.Pointer);
			break;

		default:
			break;
	}

	// Destroy sort key as well
	switch (List->KeyType) {
		case KeyPointer:
		case KeyString:
			if (Node->Key.Pointer != Node->SortKey.Pointer)
				dsfree(Node->SortKey.Pointer);
			break;

		default:
			break;
	}

	// Cleanup node and return
	dsfree(Node);
	return OsSuccess;
}

/* ListInsertAt
 * Insert the node into a specific position in the list, if position is invalid it is
 * inserted at the back. This function is not available for sorted lists, it will simply 
 * call ListInsert instead */
OsStatus_t
ListInsertAt(
	_In_ List_t *List, 
	_In_ ListNode_t *Node, 
	_In_ int Position)
{
	// Sanitize parameters
	if (List == NULL || Node == NULL) {
		return OsError;
	}

	// Redirect if we are using a insert-sorted
	// list, as we cannot insert at specific pos
	if (List->Attributes & LIST_SORT_ONINSERT) {
		return ListInsert(List, Node);
	}

	// We need to make this implementation
	_CRT_UNUSED(Position);

	// todo
	return OsSuccess;
}

/* ListInsert 
 * Inserts the node into the front of the list. This should be used for sorted
 * lists, but is available for unsorted lists aswell */
OsStatus_t
ListInsert(
	_In_ List_t *List, 
	_In_ ListNode_t *Node)
{
	// Sanitize parameters
	if (List == NULL || Node == NULL) {
		return OsError;
	}

	// Acquire lock
	if (List->Attributes & LIST_SAFE) {
		SpinlockAcquire(&List->Lock);
	}

	// So, do we need to do an insertion sort?
	if (List->Attributes & LIST_SORT_ONINSERT) {
		// Not if the list is empty of course
		if (List->Headp == NULL || List->Tailp == NULL) {
			List->Tailp = List->Headp = Node;
			Node->Link = NULL;
		}
		else
		{
			// Variables
			int Inserted = 0;

			// Iterate till we find our spot
			foreach(lNode, List) {
				// If the sort-key is larger, we continue
				if (dssortkey(List->KeyType, Node->SortKey, lNode->SortKey) == 1) {
				}
				else {
					if (lNode->Prev == NULL) {
						// Make the node point to head
						Node->Link = lNode;
						lNode->Prev = Node;
						List->Headp = Node;
					}
					else {
						// Insert between nodes 
						// lNode->Prev <---> Node <---> lNode
						lNode->Prev->Link = Node;
						Node->Prev = lNode->Prev;
						lNode->Prev = Node;
						Node->Link = lNode;
					}

					// Mark that we inserted
					Inserted = 1;
					break;
				}
			}

			// Sanity 
			// If inserted is 0, append us to 
			// back of list */
			if (!Inserted) {
				List->Tailp->Link = Node;
				Node->Prev = List->Tailp;
				List->Tailp = Node;
				Node->Link = NULL;
			}
		}
	}
	else {
		// In case the list is empty - no processing needed
		if (List->Headp == NULL || List->Tailp == NULL) {
			List->Tailp = List->Headp = Node;
			Node->Link = NULL;
		}
		else {
			// Make the node point to head
			Node->Link = List->Headp;
			List->Headp->Prev = Node;
			List->Headp = Node;
		}

		// Update previous link to NULL
		Node->Prev = NULL;
	}

	// List just got larger!
	List->Length++;

	// Release the lock
	if (List->Attributes & LIST_SAFE) {
		SpinlockRelease(&List->Lock);
	}

	// Done - no errors
	return OsSuccess;
}

/* ListAppend
 * Inserts the node into the the back of the list. This function is not
 * available for sorted lists, it will simply redirect to ListInsert */
OsStatus_t
ListAppend(
	_In_ List_t *List,
	_In_ ListNode_t *Node)
{
	// Sanitize parameters
	if (List == NULL || Node == NULL) {
		return OsError;
	}

	// Redirect if we are using a insert-sorted
	// list, as we cannot insert at specific position
	if (List->Attributes & LIST_SORT_ONINSERT) {
		return ListInsert(List, Node);
	}

	// Acquire lock
	if (List->Attributes & LIST_SAFE) {
		SpinlockAcquire(&List->Lock);
	}

	// In case of empty list just update head/tail
	if (List->Headp == NULL || List->Tailp == NULL) {
		Node->Prev = NULL;
		List->Tailp = List->Headp = Node;
	}
	else {
		// Append to tail
		List->Tailp->Link = Node;
		Node->Prev = List->Tailp;
		List->Tailp = Node;
	}

	// Always keep last link NULL
	Node->Link = NULL;
	List->Length++;

	// Release the lock
	if (List->Attributes & LIST_SAFE) {
		SpinlockRelease(&List->Lock);
	}

	// Done - no errors
	return OsSuccess;
}

/* List pop functions, the either
 * remove an element from the back or
 * the front of the given list and return
 * the node */
ListNode_t*
ListPopFront(
	_In_ List_t *List)
{
	// Variables
	ListNode_t *Current = NULL;

	// Sanitize parameters
	if (List == NULL || List->Headp == NULL) {
		return NULL;
	}

	// Acquire lock
	if (List->Attributes & LIST_SAFE) {
		SpinlockAcquire(&List->Lock);
	}

	// Manipulate the list to find the next pointer of the
	// node that comes before the one to be removed.
	Current = List->Headp;

	// Update head pointer
	List->Headp = Current->Link;

	// Set previous to null
	if (List->Headp != NULL) {
		List->Headp->Prev = NULL;
	}

	// Update tail if necessary
	if (List->Tailp == Current) {
		List->Headp = List->Tailp = NULL;
	}

	// Reset its link (remove any list traces!)
	Current->Link = NULL;
	Current->Prev = NULL;

	// Update length
	List->Length--;

	// Release the lock
	if (List->Attributes & LIST_SAFE) {
		SpinlockRelease(&List->Lock);
	}

	// Done
	return Current;
}

/* List pop functions, the either
 * remove an element from the back or
 * the front of the given list and return
 * the node */
ListNode_t*
ListPopBack(
	_In_ List_t *List)
{
	_CRT_UNUSED(List);
	return NULL;
}

/* ListGetNodeByKey
 * These are the node-retriever functions 
 * they return the list-node by either key data or index */
ListNode_t*
ListGetNodeByKey(
	_In_ List_t *List,
	_In_ DataKey_t Key, 
	_In_ int n)
{
	// Variables
	ListNode_t *It, *Found = NULL;
	int Counter = n;

	// Sanitize parameters
	if (List == NULL || List->Headp == NULL || List->Length == 0) {
		return NULL;
	}

	// Acquire lock
	if (List->Attributes & LIST_SAFE) {
		SpinlockAcquire(&List->Lock);
	}

	// Iterate each member in the given list and
	// match on the key
	_foreach(It, List) {
		if (!dsmatchkey(List->KeyType, It->Key, Key)) {
			if (Counter == 0) {
				Found = It;
				break;
			}
			else {
				Counter--;
			}
		}
	}

	// Release lock
	if (List->Attributes & LIST_SAFE) {
		SpinlockRelease(&List->Lock);
	}

	// Return whatever found
	return Found;
}

/* These are the data-retriever functions 
 * they return the list-node by either key
 * node or index */
void*
ListGetDataByKey(
	_In_ List_t *List, 
	_In_ DataKey_t Key, 
	_In_ int n)
{
	// Reuse our get-node function
	ListNode_t *Node = ListGetNodeByKey(List, Key, n);

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
ListExecuteOnKey(
	_In_ List_t *List, 
	_In_ void(*Function)(void*, int, void*), 
	_In_ DataKey_t Key, 
	_In_ void *UserData)
{
	// Variables
	ListNode_t *Node = NULL;
	int Itr = 0;

	// Sanitize parameters
	if (List == NULL || List->Headp == NULL || List->Length == 0) {
		return;
	}

	// Iterate the list and match key
	_foreach(Node, List) {
		if (!dsmatchkey(List->KeyType, Node->Key, Key)) {
			Function(Node->Data, Itr++, UserData);
		}
	}
}

/* These functions execute a given function
 * on all relevant nodes (see names) */
void
ListExecuteAll(
	_In_ List_t *List, 
	_In_ void(*Function)(void*, int, void*), 
	_In_ void *UserData)
{
	// Variables
	ListNode_t *Node = NULL;
	int Itr = 0;

	// Sanitize the paramters
	if (List == NULL || List->Headp == NULL || List->Length == 0) {
		return;
	}

	// Iteate and execute function given
	_foreach(Node, List) {
		Function(Node->Data, Itr++, UserData);
	}
}

/* ListUnlinkNode
 * This functions unlinks a node and returns the next node for usage */
ListNode_t*
ListUnlinkNode(
	_In_ List_t *List, 
	_In_ ListNode_t *Node)
{
	// Sanitize parameters
	if (List == NULL || List->Headp == NULL
		|| Node == NULL) {
		return NULL;
	}

	// Acquire lock
	if (List->Attributes & LIST_SAFE) {
		SpinlockAcquire(&List->Lock);
	}

	// There are a few cases we need to handle
	// in order for this to be O(1)
	if (Node->Prev == NULL) {
		// Ok, so this means we are the
		// first node in the list. Do we have a link?
		if (Node->Link == NULL) {
			// We're the only link
			// but lets stil validate we're from this list
			if (List->Headp == Node) {
				List->Headp = List->Tailp = NULL;
				List->Length--;
			}
		}
		else {
			// We have a link this means we set headp to next
			if (List->Headp == Node) {
				List->Headp = Node->Link;
				List->Headp->Prev = NULL;
				List->Length--;
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
			if (List->Tailp == Node) {
				List->Tailp = Node->Prev;
				List->Tailp->Link = NULL;
				List->Length--;
			}
		}
		else {
			// Normal case, we just skip this
			// element without interfering with the list
			// pointers
			ListNode_t *Prev = Node->Prev;
			Prev->Link = Node->Link;
			Prev->Link->Prev = Prev;
			List->Length--;
		}
	}

	// Release the lock
	if (List->Attributes & LIST_SAFE)
		SpinlockRelease(&List->Lock);

	// Return the next node
	if (Node->Prev == NULL) {
		return List->Headp;
	}
	else {
		return Node->Link;
	}
}

/* ListRemove
 * These are the deletion functions 
 * and remove based on either node index or key */
OsStatus_t
ListRemoveByNode(
	_In_ List_t *List,
	_In_ ListNode_t* Node)
{
	// Sanitize params
	if (List == NULL || Node == NULL) {
		return OsError;
	}

	// Reuse the unlink function and discard
	// the return value
	ListUnlinkNode(List, Node);

	// Update links
	Node->Link = NULL;
	Node->Prev = NULL;

	// Done - no errors
	return OsSuccess;
}

/* ListRemove
 * These are the deletion functions 
 * and remove based on either node index or key */
OsStatus_t
ListRemoveByIndex(
	_In_ List_t *List, 
	_In_ int Index)
{
	_CRT_UNUSED(List);
	_CRT_UNUSED(Index);
	return OsSuccess;
}

/* ListRemove
 * These are the deletion functions 
 * and remove based on either node index or key */
OsStatus_t
ListRemoveByKey(
	_In_ List_t *List, 
	_In_ DataKey_t Key)
{
	// Variables	
	ListNode_t *Node = NULL;

	// Sanitize list
	if (List == NULL) {
		return OsError;
	}

	// Lookup node
	Node = ListGetNodeByKey(List, Key, 0);

	// If found, unlink it and destroy it
	if (Node != NULL) {
		if (ListRemoveByNode(List, Node) != OsSuccess
			|| ListDestroyNode(List, Node) != OsSuccess) {
			return OsError;
		}
		else {
			return OsSuccess;
		}
	}

	// Node wasn't found
	return OsError;
}
