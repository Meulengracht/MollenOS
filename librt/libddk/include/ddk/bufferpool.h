/**
 * Copyright 2017, Philip Meulengracht
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
 * BufferPool Support Definitions & Structures
 * - This header describes the base bufferpool-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __DMA_POOL_H__
#define __DMA_POOL_H__

#include <ddk/ddkdefs.h>
#include <os/types/handle.h>

_CODE_BEGIN

struct dma_pool;

/**
 * @brief Create CPU suballocations within a mapped shared-memory buffer.
 *
 * The caller must keep the SHM attachment and its CPU mapping until the pool is
 * destroyed. This helper does not prepare DMA or expose device addresses.
 * @param shm Mapped buffer used for all pool allocations.
 * @param poolOut Receives the pool on success.
 * @return OS_EOK, or an argument/allocation error.
 */
DDKDECL(oserr_t,
dma_pool_create(
    _In_  OSHandle_t*       shm,
    _Out_ struct dma_pool** poolOut));

/* BufferPoolDestroy
 * Cleans up the buffer-pool and deallocates resources previously
 * allocated. This does not destroy the buffer-object. */
DDKDECL(oserr_t,
dma_pool_destroy(
    _In_ struct dma_pool* pool));

/**
 * @brief Allocate CPU-accessible bytes within the pool.
 * @param pool Pool created from a mapped SHM buffer.
 * @param length Number of bytes to allocate.
 * @param address_out Receives a CPU pointer. Use dma_pool_offset for SHM I/O.
 * @return OS_EOK or OS_EOOM when the pool cannot fit the allocation.
 */
DDKDECL(oserr_t,
dma_pool_allocate(
    _In_  struct dma_pool* pool,
    _In_  size_t           length,
    _Out_ void**           address_out));

/* BufferPoolFree
 * Frees previously allocations made by the buffer-pool. The virtual
 * address must be the one passed back. */
DDKDECL(oserr_t,
dma_pool_free(
    _In_ struct dma_pool* pool,
    _In_ void*            address));

DDKDECL(uuid_t,
dma_pool_handle(
    _In_ struct dma_pool* pool));    

DDKDECL(size_t,
dma_pool_offset(
    _In_ struct dma_pool* pool,
    _In_ void*            address));

_CODE_END
#endif //!__DMA_POOL_H__
