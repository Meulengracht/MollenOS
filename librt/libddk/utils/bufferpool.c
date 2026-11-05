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
#include <os/mollenos.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct dma_pool {
    struct bytepool* pool;
    OSHandle_t       handle;
    size_t           Alignment;
};

oserr_t
dma_pool_create(
    _In_  OSHandle_t*       shm,
    _Out_ struct dma_pool** poolOut)
{
    struct dma_pool*  pool;
    OSSystemCPUInfo_t cacheInfo;
    size_t            queried;
    oserr_t           oserr;

    if (shm == NULL || poolOut == NULL) {
        return OS_EINVALPARAMS;
    }

    oserr = OSSystemQuery(
        OSSYSTEMQUERY_CPUINFO,
        &cacheInfo,
        sizeof(OSSystemCPUInfo_t),
        &queried
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    if (queried != sizeof(cacheInfo) || cacheInfo.DataCacheLineSizeBytes == 0 ||
        (cacheInfo.DataCacheLineSizeBytes & (cacheInfo.DataCacheLineSizeBytes - 1)) != 0) {
        return OS_EINVALPARAMS;
    }

    // The byte allocator uses signed lengths and adds its own block headers.
    if (SHMBufferLength(shm) > LONG_MAX / 2) {
        return OS_EINVALPARAMS;
    }

    pool = (struct dma_pool*)malloc(sizeof(struct dma_pool));
    if (!pool) {
        return OS_EOOM;
    }

    // The pool uses CPU addresses and SHM byte offsets. The controller maps
    // each submitted range for DMA, so we do not need to query physical pages here.
    memcpy(&pool->handle, shm, sizeof(OSHandle_t));
    pool->Alignment = cacheInfo.DataCacheLineSizeBytes;

    // bpool requires NULL when creating the first region. A non-NULL pointer
    // tells it to extend an existing pool.
    pool->pool = NULL;
    oserr = bpool(SHMBuffer(shm), (long)SHMBufferLength(shm), &pool->pool);
    if (oserr != OS_EOK) {
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
    void*     allocation;
    uintptr_t payload;
    size_t    rounded;
    size_t    total;
    size_t    mask = pool->Alignment - 1;

    if (length == 0) {
        return OS_EINVALPARAMS;
    }

    if (length > SIZE_MAX - mask) {
        return OS_EOOM;
    }

    rounded = (length + mask) & ~mask;
    if (mask > SIZE_MAX - sizeof(void*) || rounded > SIZE_MAX - mask - sizeof(void*)) {
        return OS_EOOM;
    }

    total = rounded + mask + sizeof(void*);
    if (total > SHMBufferLength(&pool->handle)) {
        return OS_EOOM;
    }

    allocation = bget(pool->pool, (long)total);
    if (allocation == NULL) {
        return OS_EOOM;
    }

    // Align the payload and round its size to whole cache lines. Keep the saved
    // pointer and bget headers outside those lines so the pool can allocate or
    // free other buffers while DMA uses this one.
    payload = ((uintptr_t)allocation + sizeof(void*) + mask) & ~(uintptr_t)mask;
    memcpy((void*)(payload - sizeof(void*)), &allocation, sizeof(allocation));
    *address_out = (void*)payload;
    return OS_EOK;
}

oserr_t
dma_pool_free(
    _In_ struct dma_pool* pool,
    _In_ void*            address)
{
    void* allocation;

    memcpy(
        &allocation,
        (const uint8_t*)address - sizeof(void*),
        sizeof(void*)
    );
    brel(pool->pool, allocation);
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
