/**
 * MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * Linked List Implementation
 *  - Implements standard list functionality. List is implemented using a doubly
 *    linked structure, allowing O(1) add/remove, and O(n) lookups.
 */

#include <assert.h>
#include <ds/list.h>
#include <string.h>

#define LIST_LOCK   SYNC_LOCK(list)
#define LIST_UNLOCK SYNC_UNLOCK(list)

// return 0 on equal
int
list_cmp_default(void* element_key, void* key)
{
    return element_key != key;
}

int
list_cmp_string(void* element_key, void* key)
{
    return strcmp((const char*)element_key, (const char*)key);
}

void
list_construct(
    _In_ list_t* list)
{
    list_construct_cmp(list, list_cmp_default);
}

void
list_construct_cmp(
    _In_ list_t*     list,
    _In_ list_cmp_fn cmp_fn)
{
    assert(list != NULL);
    assert(cmp_fn != NULL);
    
    list->head  = NULL;
    list->tail  = NULL;
    list->cmp   = cmp_fn;
    list->count = 0;
    SYNC_INIT_FN(list);
}

void
list_clear(
    _In_ list_t* list,
    _In_ void   (*cleanup)(element_t*, void*),
    _In_ void*   context)
{
    element_t* i;
    assert(list != NULL);
    
    LIST_LOCK;
    _foreach(i, list) {
        cleanup(i, context);
    }
    list_construct_cmp(list, list->cmp);
    LIST_UNLOCK;
}

int
list_count(
    _In_ list_t* list)
{
    int count;
    assert(list != NULL);
    
    LIST_LOCK;
    count = list->count;
    LIST_UNLOCK;
    return count;
}

int
list_append(
    _In_ list_t*    list,
    _In_ element_t* element)
{
    assert(list != NULL);
    assert(element != NULL);
    
    LIST_LOCK;
    element->next = NULL;
    element->previous = list->tail;

    if (!list->head) {
        list->tail = element;
        list->head = element;
    }
    else {
        list->tail->next = element;
        list->tail       = element;
    }
    list->count++;
    LIST_UNLOCK;
    return 0;
}

static int
__list_remove(
    _In_  list_t*    list, 
    _In_  element_t* element)
{
    if (!element->previous) {
        // Ok, so this means we are the
        // first node in the list. Do we have a link?
        if (!element->next) {
            // We're the only link but lets stil validate we're from the list
            if (list->head == element) {
                list->head = list->tail = NULL;
                list->count--;
                return 0;
            }
        }
        else {
            // We have a link this means we set head to next
            if (list->head == element) {
                list->head = element->next;
                list->head->previous = NULL;
                list->count--;
                return 0;
            }
        }
    }
    else {
        // We have a previous,
        // Special case 1: we are last element
        // which means we should update pointer
        if (!element->next) {
            // Ok, we are last element 
            // Update tail pointer to previous
            if (list->tail == element) {
                list->tail = element->previous;
                list->tail->next = NULL;
                list->count--;
                return 0;
            }
        }
        else {
            // Normal case, we just skip this
            // element without interfering with the list
            // pointers
            element_t* previous = element->previous;
            previous->next = element->next;
            previous->next->previous = previous;
            list->count--;
            return 0;
        }
    }
    return -1;
}

int
list_remove(
    _In_ list_t*    list,
    _In_ element_t* element)
{
    int status;
    
    assert(list != NULL);
    assert(element != NULL);
    
    LIST_LOCK;
    status = __list_remove(list, element);
    LIST_UNLOCK;
    return status;
}

void
list_splice(
    _In_ list_t* list_in,
    _In_ int     count,
    _In_ list_t* list_out)
{
    element_t* head;
    element_t* tail;
    list_t*    list;
    int        element_count;
    
    assert(list_in != NULL);
    assert(list_out != NULL);
    
    list = list_in;
    LIST_LOCK;
    head          = list->head;
    element_count = MIN(list->count, count);
    if (!element_count) {
        LIST_UNLOCK;
        return;
    }
    
    list->count  -= element_count;
    while (element_count--) {
        tail       = list->head;
        list->head = list->head->next;
    }
    
    if (!list->head) {
        list->tail = NULL;
    }
    LIST_UNLOCK;
    
    list = list_out;
    LIST_LOCK;
    if (!list->head) {
        list->head = head;
    }
    else {
        list->tail->next = head;
    }
    list->tail = tail;
    LIST_UNLOCK;
}

element_t*
list_find(
    _In_ list_t* list,
    _In_ void*   key)
{
    element_t* i;
    assert(list != NULL);
    
    LIST_LOCK;
    _foreach(i, list) {
        if (!list->cmp(i->key, key)) {
            break;
        }
    }
    LIST_UNLOCK;
    return i;
}

element_t*
list_front(
    _In_ list_t* list)
{
    element_t* front;
    assert(list != NULL);
    
    LIST_LOCK;
    front = list->head;
    LIST_UNLOCK;
    return front;
}

void*
list_find_value(
    _In_ list_t* list,
    _In_ void*   key)
{
    element_t* element = list_find(list, key);
    if (!element) {
        return NULL;
    }
    return element->value;
}

void
list_enumerate(
    _In_ list_t* list,
    _In_ int   (*callback)(int, element_t*, void*),
    _In_ void*   context)
{
    element_t* i;
    int        idx = 0;
    
    assert(list != NULL);
    
    LIST_LOCK;
    _foreach_nolink(i, list) {
        int action = callback(idx, i, context);
        if (action & LIST_ENUMERATE_REMOVE) {
            element_t* j = i->next;
            __list_remove(list, i);
            i = j;
        }
        else {
            i = i->next;
        }
        
        if (action & LIST_ENUMERATE_STOP) {
            break;
        }
        idx++;
    }
    LIST_UNLOCK;
}
