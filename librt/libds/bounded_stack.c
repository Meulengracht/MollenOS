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
 * Managed Bound Stack Implementation
 *  - Implements a managed lockfree statck. The stack is of fixed size.
 */

#include <assert.h>
#include <ds/bounded_stack.h>

void
bounded_stack_construct(
    _In_ bounded_stack_t* stack,
    _In_ void**           storage,
    _In_ int              capacity)
{
    assert(stack != NULL);
    
    stack->capacity = capacity;
    stack->elements = storage;
    stack->index    = 0;
}

int
bounded_stack_push(
    _In_ bounded_stack_t* stack,
    _In_ void*            element)
{
    int result = -1;
    assert(stack != NULL);
    
    if (stack->index < stack->capacity) {
        stack->elements[stack->index++] = element;
        result = 0;
    }
    return result;
}

int
bounded_stack_push_multiple(
    _In_ bounded_stack_t* stack,
    _In_ void**           elements,
    _In_ int              count)
{
    int i;
    assert(stack != NULL);
    
    for (i = 0; i < count; i++) {
        if (stack->index == stack->capacity) {
            break;
        }
        stack->elements[stack->index++] = elements[i];
    }
    return i;
}

void*
bounded_stack_pop(
    _In_ bounded_stack_t* stack)
{
    void* element = NULL;
    assert(stack != NULL);
    
    if (stack->index > 0) {
        element = stack->elements[--stack->index];
    }
    return element;
}

int
bounded_stack_pop_multiple(
    _In_ bounded_stack_t* stack,
    _In_ void**           elements,
    _In_ int              count)
{
    int i;
    assert(stack != NULL);
    
    for (i = 0; i < count; i++) {
        if (stack->index == 0) {
            break;
        }
        elements[i] = stack->elements[--stack->index];
    }
    return i;
}
