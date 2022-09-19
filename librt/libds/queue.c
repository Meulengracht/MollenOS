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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Queue Implementation
 *  - Implements standard queue functionality. Queue is implemented using a doubly
 *    linked structure, allowing O(1) push/pop operations.
 */

#include <assert.h>
#include <ddk/io.h>
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
    _foreach_volatile(i, queue) {
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

    // To avoid compiler optimizations here, we need to enforce the control
    // dependencies that can be optimized away by certian compilers. Because
    // we also do identical writes in the if/else, we also enforce a memory
    // barrier
    if (!READ_VOLATILE(queue->head)) {
        WRITE_VOLATILE(queue->tail, element);
        WRITE_VOLATILE(queue->head, element);
    }
    else {
        WRITE_VOLATILE(queue->tail->next, element);
        WRITE_VOLATILE(queue->tail, element);
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
    element = READ_VOLATILE(queue->head);
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
    element = READ_VOLATILE(queue->head);
    QUEUE_UNLOCK;
    return element;
}
