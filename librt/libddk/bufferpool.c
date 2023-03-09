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
#include <os/shm.h>
#include <stdlib.h>
#include <string.h>

struct dma_pool {
    struct bytepool* pool;
    OSHandle_t       handle;
    SHMSGTable_t     table;
};

oserr_t
dma_pool_create(
    _In_  OSHandle_t*       shm,
    _Out_ struct dma_pool** poolOut)
{
    struct dma_pool* pool;
    oserr_t          oserr;
    
    if (shm == NULL || poolOut == NULL) {
        return OS_EINVALPARAMS;
    }
    
    pool = (struct dma_pool*)malloc(sizeof(struct dma_pool));
    if (!pool) {
        return OS_EOOM;
    }
    
    memcpy(&pool->handle, shm, sizeof(OSHandle_t));

    oserr = SHMGetSGTable(shm, &pool->table, -1);
    if (oserr != OS_EOK) {
        free(pool);
        return oserr;
    }

    oserr = bpool(SHMBuffer(shm), (long)SHMBufferLength(shm), &pool->pool);
    if (oserr != OS_EOK) {
        free(pool->table.Entries);
        free(pool);
        return oserr;
    }

    *poolOut = pool;
    return oserr;
}

oserr_t
dma_pool_destroy(
    _In_ struct dma_pool* pool)
{
    free(pool->table.Entries);
    free(pool->pool);
    free(pool);
    return OS_EOK;
}

oserr_t
dma_pool_allocate(
    _In_  struct dma_pool* pool,
    _In_  size_t           length,
    _Out_ void**           address_out)
{
    void* allocation;

    TRACE("dma_pool_allocate(Size %u)", length);

    allocation = bget(pool->pool, (long)length);
    if (!allocation) {
        ERROR("Failed to allocate bufferpool memory (size %u)", length);
        return OS_EOOM;
    }
    
    *address_out = allocation;
    return OS_EOK;
}

oserr_t
dma_pool_free(
    _In_ struct dma_pool* pool,
    _In_ void*            address)
{
    brel(pool->pool, address);
    return OS_EOK;
}

uuid_t
dma_pool_handle(
    _In_ struct dma_pool* pool)
{
    if (pool == NULL) {
        return UUID_INVALID;
    }
    return pool->handle.ID;
}

size_t
dma_pool_offset(
    _In_ struct dma_pool* pool,
    _In_ void*            address)
{
    if (pool == NULL || !address) {
        return 0;
    }
    return (uintptr_t)address - (uintptr_t)SHMBuffer(&pool->handle);
}
