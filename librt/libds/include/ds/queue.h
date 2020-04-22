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
 * Queue Implementation
 *  - Implements standard queue functionality. Queue is implemented using a doubly
 *    linked structure, allowing O(1) push/pop operations.
 */

#ifndef __LIBDS_QUEUE_H__
#define __LIBDS_QUEUE_H__

#include <ds/dsdefs.h>
#include <ds/shared.h>
#include <stdint.h>

typedef struct queue {
    struct element* head;
    struct element* tail;
    size_t          count;
    syncobject_t    lock;
} queue_t;

#define QUEUE_INIT { NULL, NULL, 0, SYNC_INIT }

DSDECL(void,
queue_construct(
    _In_ queue_t*));

DSDECL(void,
queue_clear(
    _In_ queue_t*,
    _In_ void(*)(element_t*)));

DSDECL(int,
queue_push(
    _In_ queue_t*,
    _In_ element_t*));

DSDECL(element_t*,
queue_pop(
    _In_ queue_t*));

DSDECL(element_t*,
queue_peek(
    _In_ queue_t*));

#endif //!__LIBDS_QUEUE_H__
