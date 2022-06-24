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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * BufferPool Support Definitions & Structures
 * - This header describes the base bufferpool-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */
//#define __TRACE

#include <ddk/bufferpool.h>
#include <ddk/bytepool.h>
#include <ddk/utils.h>
#include <os/dmabuf.h>
#include <stdlib.h>

struct dma_pool {
    struct bytepool*       pool;
    struct dma_attachment* attachment;
    struct dma_sg_table    table;
};

OsStatus_t
dma_pool_create(
    _In_  struct dma_attachment* attachment,
    _Out_ struct dma_pool**      pool_out)
{
    struct dma_pool* pool;
    OsStatus_t       status;
    
    if (!attachment || !pool_out || !attachment->buffer) {
        return OsInvalidParameters;
    }
    
    pool = (struct dma_pool*)malloc(sizeof(struct dma_pool));
    if (!pool) {
        return OsOutOfMemory;
    }
    
    pool->attachment = attachment;
    
    status = dma_get_sg_table(attachment, &pool->table, -1);
    status = bpool(attachment->buffer, attachment->length, &pool->pool);
    
    *pool_out = pool;
    return status;
}

OsStatus_t
dma_pool_destroy(
    _In_ struct dma_pool* pool)
{
    free(pool->table.entries);
    free(pool->pool);
    free(pool);
    return OsOK;
}

static uintptr_t
dma_pool_get_dma(
    _In_ struct dma_pool* pool,
    _In_ size_t           offset)
{
    int        entry_index;
    size_t     sg_offset;
    OsStatus_t status = dma_sg_table_offset(
        &pool->table, offset, &entry_index, &sg_offset);
    return status != OsOK ? 0 : pool->table.entries[entry_index].address + sg_offset;
}

OsStatus_t
dma_pool_allocate(
    _In_  struct dma_pool* pool,
    _In_  size_t           length,
    _Out_ void**           address_out)
{
    void* allocation;

    TRACE("dma_pool_allocate(Size %u)", length);

    allocation = bget(pool->pool, length);
    if (!allocation) {
        ERROR("Failed to allocate bufferpool memory (size %u)", length);
        return OsOutOfMemory;
    }
    
    *address_out = allocation;
    return OsOK;
}

OsStatus_t
dma_pool_free(
    _In_ struct dma_pool* pool,
    _In_ void*            address)
{
    brel(pool->pool, address);
    return OsOK;
}

UUId_t
dma_pool_handle(
    _In_ struct dma_pool* pool)
{
    if (!pool || !pool->attachment) {
        return UUID_INVALID;
    }
    return pool->attachment->handle;
}

size_t
dma_pool_offset(
    _In_ struct dma_pool* pool,
    _In_ void*            address)
{
    if (!pool || !pool->attachment || !address) {
        return 0;
    }
    return (uintptr_t)address - (uintptr_t)pool->attachment->buffer;
}
