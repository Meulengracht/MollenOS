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

#include <assert.h>
#include <ds/queue.h>

#define QUEUE_LOCK   SYNC_LOCK(queue)
#define QUEUE_UNLOCK SYNC_UNLOCK(queue)

void
queue_construct(
    _In_ queue_t* queue)
{
    assert(queue != NULL);
    
    queue->head  = NULL;
    queue->tail  = NULL;
    queue->count = 0;
    SYNC_INIT_FN(queue);
}

void
queue_clear(
    _In_ queue_t* queue,
    _In_ void    (*cleanup)(element_t*))
{
    element_t* i;
    assert(queue != NULL);
    
    QUEUE_LOCK;
    _foreach(i, queue) {
        cleanup(i);
    }
    queue_construct(queue);
    QUEUE_UNLOCK;
}

int
queue_push(
    _In_ queue_t*   queue,
    _In_ element_t* element)
{
    assert(queue != NULL);
    assert(element != NULL);
    
    QUEUE_LOCK;
    element->next = NULL;
    element->previous = queue->tail;

    if (!queue->head) {
        queue->tail = element;
        queue->head = element;
    }
    else {
        queue->tail->next = element;
        queue->tail       = element;
    }
    queue->count++;
    QUEUE_UNLOCK;
    return 0;
}

element_t*
queue_pop(
    _In_ queue_t* queue)
{
    element_t* element;
    assert(queue != NULL);
    
    QUEUE_LOCK;
    element = queue->head;
    if (element) {
        queue->head = element->next;
        if (!queue->head) {
            queue->tail = NULL;
        }
        queue->count--;
    }
    QUEUE_UNLOCK;
    return element;
}

element_t*
queue_peek(
    _In_ queue_t* queue)
{
    element_t* element;
    assert(queue != NULL);
    QUEUE_LOCK;
    element = queue->head;
    QUEUE_UNLOCK;
    return element;
}
