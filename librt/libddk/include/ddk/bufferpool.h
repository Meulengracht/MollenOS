/**
 * MollenOS
 *
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

struct dma_attachment;
struct dma_pool;

_CODE_BEGIN
/* BufferPoolCreate
 * Creates a new buffer-pool from the given buffer object. 
 * This allows sub-allocations from a buffer-object. */
DDKDECL(oscode_t,
        dma_pool_create(
    _In_  struct dma_attachment* attachment,
    _Out_ struct dma_pool**      pool_out));

/* BufferPoolDestroy
 * Cleans up the buffer-pool and deallocates resources previously
 * allocated. This does not destroy the buffer-object. */
DDKDECL(oscode_t,
        dma_pool_destroy(
    _In_ struct dma_pool* pool));

/* BufferPoolAllocate
 * Allocates the requested size and outputs two addresses. The
 * virtual pointer to the accessible data, and the address of its 
 * corresponding physical address for hardware. */
DDKDECL(oscode_t,
        dma_pool_allocate(
    _In_  struct dma_pool* pool,
    _In_  size_t           length,
    _Out_ void**           address_out));

/* BufferPoolFree
 * Frees previously allocations made by the buffer-pool. The virtual
 * address must be the one passed back. */
DDKDECL(oscode_t,
        dma_pool_free(
    _In_ struct dma_pool* pool,
    _In_ void*            address));

DDKDECL(UUId_t,
dma_pool_handle(
    _In_ struct dma_pool* pool));    

DDKDECL(size_t,
dma_pool_offset(
    _In_ struct dma_pool* pool,
    _In_ void*            address));
_CODE_END

#endif //!__DMA_POOL_H__
