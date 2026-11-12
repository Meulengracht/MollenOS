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

#ifndef __OS_TYPES_SHM_REGION_H__
#define __OS_TYPES_SHM_REGION_H__

#include <os/types/buffer_identity.h>
#include <os/types/shm.h>

#define SHM_REGION_LIMIT       4096U
#define SHM_REGION_TOKEN_LIMIT 4096U

/** Sorted, nonoverlapping logical byte ranges. Both ends must be cache-line aligned. */
struct SHMRegionRange {
    size_t Offset;
    size_t Length;
};

/** Prepared CPU session bound to one complete region. Zero before opening. */
struct SHMRegionSession {
    uuid_t             Handle;
    OSBufferIdentity_t Identity;
    unsigned int       TokenCapacity;
};

#endif //!__OS_TYPES_SHM_REGION_H__
