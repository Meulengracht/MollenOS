/* MollenOS
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * BufferPool Support Definitions & Structures
 * - This header describes the base bufferpool-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */
//#define __TRACE

#include <ddk/bufferpool.h>
#include <ddk/utils.h>
#include <os/dmabuf.h>
#include "../common/bytepool.h"
#include <stdlib.h>

struct dma_pool {
    BytePool_t*            pool;
    struct dma_attachment* attachment;
    int                    sg_size;
    struct dma_sg          sg_list[1];
};

OsStatus_t
dma_pool_create(
    _In_  struct dma_attachment* attachment,
    _Out_ struct dma_pool**      pool_out)
{
    struct dma_pool* pool;
    OsStatus_t       status;
    int              sg_size;
    
    if (!attachment || !pool_out || !attachment->buffer) {
        return OsInvalidParameters;
    }
    
    status = dma_get_metrics(attachment, &sg_size, NULL);
    if (status != OsSuccess) {
        return status;
    }
    
    pool             = (struct dma_pool*)malloc(sizeof(struct dma_pool) + (sg_size * sizeof(struct dma_sg)));
    pool->attachment = attachment;
    pool->sg_size    = sg_size;
    
    status = dma_get_metrics(attachment, NULL, &pool->sg_list[0]);
    status = bpool(attachment->buffer, attachment->length, &pool->pool);
    
    *pool_out = pool;
    return status;
}

OsStatus_t
dma_pool_destroy(
    _In_ struct dma_pool* pool)
{
    free(pool->pool);
    free(pool);
    return OsSuccess;
}

static uintptr_t
dma_pool_get_dma(
    _In_ struct dma_pool* pool,
    _In_ size_t           offset)
{
    int i;
    for (i = 0; i < pool->sg_size; i++) {
        if (offset < pool->sg_list[i].length) {
            return pool->sg_list[i].address + offset;
        }
        offset -= pool->sg_list[i].length;
    }
    return 0;
}

OsStatus_t
dma_pool_allocate(
    _In_  struct dma_pool* pool,
    _In_  size_t           length,
    _Out_ void**           address_out,
    _Out_ uintptr_t*       dma_address_out)
{
    ptrdiff_t difference;
    void*     allocation;

    TRACE("dma_pool_allocate(Size %u)", length);

    allocation = bget(pool->pool, length);
    if (!allocation) {
        ERROR("Failed to allocate bufferpool memory (size %u)", length);
        return OsOutOfMemory;
    }
    
    difference       = (uintptr_t)allocation - (uintptr_t)pool->attachment->buffer;
    *address_out     = allocation;
    *dma_address_out = dma_pool_get_dma(pool, difference);
    TRACE(" > Virtual address 0x%x => Physical address 0x%x", allocation, *dma_address_out);
    return OsSuccess;
}

OsStatus_t
dma_pool_free(
    _In_ struct dma_pool* pool,
    _In_ void*            address)
{
    brel(pool->pool, address);
    return OsSuccess;
}
