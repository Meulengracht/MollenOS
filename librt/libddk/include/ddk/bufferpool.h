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
 * @brief Creates an allocation pool within a mapped SHM buffer.
 *
 * The SHM buffer must stay attached and mapped until the pool is destroyed.
 * The pool uses CPU addresses. DMA setup is handled separately.
 *
 * @param shm Mapped buffer used for all pool allocations.
 * @param poolOut Receives the pool on success.
 * @return OS_EOK, or an argument, allocation or CPU information query error.
 */
DDKDECL(oserr_t,
dma_pool_create(
    _In_  OSHandle_t*       shm,
    _Out_ struct dma_pool** poolOut));

/**
 * @brief Frees the pool and its allocator data. The SHM buffer remains valid.
 *
 * @param pool Pool to destroy.
 * @return OS_EOK.
 */
DDKDECL(oserr_t,
dma_pool_destroy(
    _In_ struct dma_pool* pool));

/**
 * @brief Allocates CPU-accessible memory on separate cache lines within the pool.
 *
 * The caller must serialize allocation and free calls. Wait for all DMA use of
 * an allocation to complete before freeing it. Allocations do not share cache
 * lines with each other or with allocator data.
 *
 * @param pool Pool created from a mapped SHM buffer.
 * @param length Number of bytes to allocate.
 * @param address_out Receives a CPU pointer. Use dma_pool_offset for SHM I/O.
 * @return OS_EOK, OS_EINVALPARAMS for zero length, or OS_EOOM if the pool cannot fit it.
 */
DDKDECL(oserr_t,
dma_pool_allocate(
    _In_  struct dma_pool* pool,
    _In_  size_t           length,
    _Out_ void**           address_out));

/**
 * @brief Frees an allocation from the pool.
 * All DMA use of the allocation must have completed before this call.
 *
 * @param pool Pool the allocation came from.
 * @param address CPU pointer returned by dma_pool_allocate.
 * @return OS_EOK.
 */
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
