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
 * Managed Bound Stack Implementation
 *  - Implements a managed stack. The stack is of fixed size.
 */

#ifndef __DS_BOUNDED_STACK_H__
#define __DS_BOUNDED_STACK_H__

#include <ds/shared.h>

typedef struct bounded_stack {
    int          capacity;
    int          index;
    void**       elements;
} bounded_stack_t;

#define BOUNDED_STACK_INIT(capacity, storage) { capacity, 0, storage }

_CODE_BEGIN

CRTDECL(void,
bounded_stack_construct(
    _In_ bounded_stack_t*,
    _In_ void**,
    _In_ int));

CRTDECL(int,
bounded_stack_push(
    _In_ bounded_stack_t*,
    _In_ void*));

CRTDECL(int,
bounded_stack_push_multiple(
    _In_ bounded_stack_t*,
    _In_ void**,
    _In_ int));

CRTDECL(void*,
bounded_stack_pop(
    _In_ bounded_stack_t*));

CRTDECL(int,
bounded_stack_pop_multiple(
    _In_ bounded_stack_t*,
    _In_ void**,
    _In_ int));

_CODE_END

#endif //!__DS_BOUNDED_STACK_H__
