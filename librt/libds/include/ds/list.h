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

#define LIST_INIT { NULL, NULL, NULL, 0, SYNC_INIT }

CRTDECL(void,
list_construct(
    _In_ list_t*));

CRTDECL(void,
list_construct_cmp(
    _In_ list_t*,
    _In_ list_cmp_fn));

CRTDECL(void,
list_clear(
    _In_ list_t*,
    _In_ void(*)(element_t*)));

CRTDECL(int,
list_append(
    _In_ list_t*,
    _In_ element_t*));

CRTDECL(int,
list_remove(
    _In_ list_t*,
    _In_ element_t*));

CRTDECL(void*,
list_get_value(
    _In_ list_t*,
    _In_ void*));

CRTDECL(void,
list_enumerate(
    _In_ list_t*,
    _In_ void(*)(int, element_t*, void*),
    _In_ void*));

#endif //!__LIBDS_LIST_H__
