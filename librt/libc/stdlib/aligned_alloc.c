/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS C-Support Aligned Allocation Implementation
 * - Definitions, prototypes and information needed.
 */
#include <stdlib.h>
#include <errno.h>

void* aligned_alloc(
    _In_ size_t alignment,
    _In_ size_t size)
{
    size_t total_size = alignment + size;
    void *ptr = malloc(total_size);
    if (ptr == NULL) {
        _set_errno(ENOMEM);
        return NULL;
    }
    return (void*)ALIGN((uintptr_t)ptr, alignment, 1);
}
