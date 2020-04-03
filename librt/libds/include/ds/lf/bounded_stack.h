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
 * Managed Lockfree Stack Implementation
 *  - Implements a managed lockfree stack. The stack is of fixed size.
 */

#ifndef __DS_LF_BOUNDED_STACK_H__
#define __DS_LF_BOUNDED_STACK_H__

#include <ds/dsdefs.h>
#include <ds/shared.h>

struct lf_bounded_stack_node {
    void*                         value;
    struct lf_bounded_stack_node* next;
};

struct lf_bounded_stack_head {
    uintptr_t                     aba;
    struct lf_bounded_stack_node* node;
};

typedef struct lf_bounded_stack {
    struct lf_bounded_stack_node*         nodes;
    _Atomic(struct lf_bounded_stack_head) head;
    _Atomic(struct lf_bounded_stack_head) free;
    _Atomic(int)                          count;
} lf_bounded_stack_t;

_CODE_BEGIN

DSDECL(int,   lf_bounded_stack_construct(lf_bounded_stack_t*, int));
DSDECL(void,  lf_bounded_stack_destroy(lf_bounded_stack_t*));
DSDECL(int,   lf_bounded_stack_push(lf_bounded_stack_t*, void*));
DSDECL(void*, lf_bounded_stack_pop(lf_bounded_stack_t*));

_CODE_END

#endif //!__DS_LF_BOUNDED_STACK_H__
