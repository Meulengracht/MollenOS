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

#ifndef __OS_TYPES_DMA_REGION_H__
#define __OS_TYPES_DMA_REGION_H__

#include <os/types/shm_region.h>
#include <os/types/dma.h>

/**
 * @brief Details returned by the kernel when a driver opens a DMA region session.
 *
 * A region session binds one driver queue to a single registered region inside
 * a DMA mapping. The DDK uses Offset and Length to cut the parent mapping's
 * device address list down to just this region, and passes Handle to begin,
 * end, and close calls.
 */
typedef struct OSDmaRegionSession {
    // Kernel handle for this region session.
    uuid_t             Handle;
    // Buffer, region index, and generation that identify the bound region.
    OSBufferIdentity_t Identity;
    // Number of reusable use slots (1 to SHM_REGION_TOKEN_LIMIT) prepared in the kernel.
    unsigned int       TokenCapacity;
    // Byte offset where this region begins within the parent DMA mapping.
    size_t             Offset;
    // Size of this region in bytes.
    size_t             Length;
} OSDmaRegionSession_t;

#endif //!__OS_TYPES_DMA_REGION_H__
