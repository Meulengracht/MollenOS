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

#ifndef __LIBDS_LIST_H__
#define __LIBDS_LIST_H__

#include <ds/dsdefs.h>
#include <ds/shared.h>
#include <stdint.h>

typedef int (*list_cmp_fn)(void*, void*);

typedef struct list {
    struct element* head;
    struct element* tail;
    list_cmp_fn     cmp;
    int             count;
    syncobject_t    lock;
} list_t;

#define LIST_INIT          { NULL, NULL, list_cmp_default, 0, SYNC_INIT }
#define LIST_INIT_CMP(cmp) { NULL, NULL, cmp, 0, SYNC_INIT }

#define LIST_ENUMERATE_CONTINUE (int)0x0
#define LIST_ENUMERATE_STOP     (int)0x1
#define LIST_ENUMERATE_REMOVE   (int)0x2

// Default provided comparators
DSDECL(int, list_cmp_default(void*,void*));
DSDECL(int, list_cmp_string(void*,void*));

DSDECL(void,
list_construct(
    _In_ list_t*));

DSDECL(void,
list_construct_cmp(
    _In_ list_t*,
    _In_ list_cmp_fn));

DSDECL(void,
list_clear(
    _In_ list_t*,
    _In_ void(*)(element_t*, void*),
    _In_ void*));

DSDECL(int,
list_count(
    _In_ list_t*));

DSDECL(int,
list_append(
    _In_ list_t*,
    _In_ element_t*));

DSDECL(int,
list_remove(
    _In_ list_t*,
    _In_ element_t*));

/**
 * list_splice
 * * Returns a spliced node list that can be appended to a new list. The list will
 * * at maximum have the requested count or the length of the list
 * @param list_in  [In] The list that will be spliced.
 * @param count    [In] The maximum number of list nodes that will be extracted.
 * @param list_out [In] The list which the number of elements will be spliced into.
 */
DSDECL(void,
list_splice(
    _In_ list_t*,
    _In_ int,
    _In_ list_t*));

DSDECL(element_t*,
list_front(
    _In_ list_t*));

DSDECL(element_t*,
       list_back(
   _In_ list_t*));

DSDECL(element_t*,
list_find(
    _In_ list_t*,
    _In_ void*));

DSDECL(void*,
list_find_value(
    _In_ list_t*,
    _In_ void*));

DSDECL(void,
list_enumerate(
    _In_ list_t*,
    _In_ int(*)(int, element_t*, void*),
    _In_ void*));

#endif //!__LIBDS_LIST_H__
