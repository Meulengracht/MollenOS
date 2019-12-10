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
 * Managed Lockfree Array Implementation
 *  - Implements a managed lockfree array. The array is of fixed size.
 */

#ifndef __DS_LF_ARRAY_H__
#define __DS_LF_ARRAY_H__

#include <ds/shared.h>

typedef struct array {
    int              capacity;
    _Atomic(void**)  elements;
} array_t;

_CODE_BEGIN

CRTDECL(void, 
array_construct(
    _In_ array_t*,
    _In_ void**,
    _In_ int));

CRTDECL(void,
array_set(
    _In_ array_t*,
    _In_ int,
    _In_ void*));

CRTDECL(void*, 
array_get(
    _In_ array_t*,
    _In_ int));

_CODE_END

#endif //!__DS_LF_ARRAY_H__
