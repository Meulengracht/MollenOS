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
 * Managed Lockfree Stack Implementation
 *  - Implements a managed lockfree stack. The stack is of fixed size.
 */

#include <ds/lf/bounded_stack.h>
#include <ds/ds.h>
#include <errno.h>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Watomic-alignment"
#endif

int
lf_bounded_stack_construct(
    _In_ lf_bounded_stack_t* stack,
    _In_ int                 capacity)
{
    struct lf_bounded_stack_head head     = { 0, NULL };
    struct lf_bounded_stack_head freeList = { 0, NULL };
    
    if (!stack || capacity <= 0) {
        errno = EINVAL;
        return -1;
    }
    
    stack->head  = head;
    stack->count = 0;

    stack->nodes = dsalloc(capacity * sizeof(struct lf_bounded_stack_node));
    if (stack->nodes == NULL) {
        errno = ENOMEM;
        return -1;
    }
    
    for (int i = 0; i < capacity - 1; i++)
        stack->nodes[i].next = stack->nodes + i + 1;
    stack->nodes[capacity - 1].next = NULL;
    
    freeList.node = stack->nodes;
    stack->free = freeList;
    return 0;
}

void
lf_bounded_stack_destroy(
    _In_ lf_bounded_stack_t* stack)
{
    if (!stack) {
        return;
    }
    dsfree(stack->nodes);
}

static struct lf_bounded_stack_node*
pop(
    _In_ _Atomic(struct lf_bounded_stack_head)* head)
{
    struct lf_bounded_stack_head next;
    struct lf_bounded_stack_head orig = atomic_load(head);
    
    do {
        if (orig.node == NULL) {
            return NULL;
        }
        
        next.aba  = orig.aba + 1;
        next.node = orig.node->next;
    } while (!atomic_compare_exchange_weak(head, &orig, next));
    return orig.node;
}

static void
push(
    _In_ _Atomic(struct lf_bounded_stack_head)* head,
    _In_ struct lf_bounded_stack_node*          node)
{
    struct lf_bounded_stack_head next;
    struct lf_bounded_stack_head orig = atomic_load(head);
    
    do {
        node->next = orig.node;
        next.aba   = orig.aba + 1;
        next.node  = node;
    } while (!atomic_compare_exchange_weak(head, &orig, next));
}

int
lf_bounded_stack_push(
    _In_ lf_bounded_stack_t* stack,
    _In_ void*               value)
{
    struct lf_bounded_stack_node *node;
    
    if (!stack) {
        errno = EINVAL;
        return -1;
    }
    
    node = pop(&stack->free);
    if (node == NULL) {
        errno = ENOMEM;
        return -1;
    }
    
    node->value = value;
    push(&stack->head, node);
    atomic_fetch_add(&stack->count, 1);
    return 0;
}

void*
lf_bounded_stack_pop(
    _In_ lf_bounded_stack_t* stack)
{
    struct lf_bounded_stack_node* node;
    void*                         value;
    
    if (!stack) {
        return NULL;
    }
    
    node = pop(&stack->head);
    if (node == NULL) {
        return NULL;
    }
    
    atomic_fetch_sub(&stack->count, 1);
    value = node->value;
    push(&stack->free, node);
    return value;
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
