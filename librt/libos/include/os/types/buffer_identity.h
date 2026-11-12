/**
 * Copyright, Philip Meulengracht
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
 */

#ifndef __OS_TYPES_BUFFER_IDENTITY_H__
#define __OS_TYPES_BUFFER_IDENTITY_H__

#include <os/osdefs.h>

/**
 * @brief Shared identity for one buffer region.
 *
 * Buffer identifies the shared buffer. Region is an agreed region number, which
 * can be zero. Callers agree on its byte range separately. Generation distinguishes
 * different uses of the identity. Buffer must not be UUID_INVALID and Generation
 * must be nonzero. Initialize the whole structure, including padding, before
 * setting fields. Keep the identity unchanged until all users and references to
 * the region finish.
 */
typedef struct OSBufferIdentity {
    uuid_t       Buffer;
    unsigned int Region;
    unsigned int Generation;
} OSBufferIdentity_t;

#endif //!__OS_TYPES_BUFFER_IDENTITY_H__
