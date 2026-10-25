/**
 * Copyright 2023, Philip Meulengracht
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

#ifndef __DDK_DMA_PRIVATE_H__
#define __DDK_DMA_PRIVATE_H__

#include <ddk/dma.h>
#include <os/types/shm.h>
#include <stdatomic.h>

struct __DmaResource {
    struct __DmaResource* Next;
    struct DMAContext*    Context;
    uuid_t                Handle;
    uint32_t              SegmentCount;
    struct DMASegment     InlineSegment;
    struct DMASegment*    Segments;
    SHMHandle_t*          Storage;
};

struct DMAContext {
    struct DMAContext*    Next;
    uuid_t                Device;
    uuid_t                Handle;
    uint64_t              Session;
    bool                  Closing;
    atomic_uint           Children;
    struct __DmaResource* Pending;
};

/**
 * @brief Retain a failed creation's cleanup under its context for close to retry.
 * 
 * @param resource Unpublished child whose release failed; ownership is transferred.
 */
void
__DmaDeferCleanup(
    _In_ struct __DmaResource* resource);

/**
 * @brief Retry unpublished children while the context registry lock is held.
 * 
 * @param context Context whose caller has excluded concurrent creation/close.
 * @return First cleanup error; remaining children stay owned for retry.
 */
oserr_t
__DmaCollect(
    _In_ struct DMAContext* context);

#endif
