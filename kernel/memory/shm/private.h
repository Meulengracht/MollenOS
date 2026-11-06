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

#ifndef __SHM_PRIVATE_H__
#define __SHM_PRIVATE_H__

#include <component/memory.h>
#include <memoryspace.h>
#include <mutex.h>
#include <os/types/shm.h>
#include <stdatomic.h>
#include "range.h"

struct SHMAccess;

/** Range reservations in submission order, protected by the buffer mutex. */
struct SHMBufferAccess {
    struct SHMAccess* First;
    struct SHMAccess* Last;
    atomic_uint       Waiting;
    atomic_uint       Faults;
};

/**
 * @brief Internal SHMBuffer representation.
 *
 * Allocation tracks reserved backing memory when present. The original memory
 * component releases it after the last buffer reference is gone.
 */
struct SHMBuffer {
    uuid_t                        ID;
    MemorySpace_t*                Owner;
    Mutex_t                       Mutex;
    struct SHMBufferAccess        Access;
    vaddr_t                       KernelMapping;
    vaddr_t                       PageMapping;
    size_t                        Offset;
    size_t                        Length;
    unsigned int                  Flags;
    struct SystemMemoryAllocation Allocation;
    int                           PageCount;
    paddr_t                       Pages[];
};

/**
 * @brief Tell whether every view of a buffer must be uncached.
 *
 * All views of the same memory must use the same caching mode to avoid stale
 * CPU data. Pool memory uses the pool's caching mode. Ordinary SHM uses cached
 * memory.
 */
static inline bool
SHMBufferUncached(
    _In_ const struct SHMBuffer* buffer)
{
    return buffer->Allocation.Owner != NULL && !buffer->Allocation.Cached;
}

/**
 * @brief Populate missing backing pages. The caller must hold a buffer reference.
 * New SHM_CLEAN pages are zeroed before they enter the backing array.
 * @param buffer Referenced SHM backing.
 * @param pageStart First page in the backing array.
 * @param pageCount Number of pages within that array.
 * @return OS_EOK or an allocation/mapping error. Earlier allocated pages stay valid.
 */
oserr_t
SHMEnsurePhysicalPages(
    _In_ struct SHMBuffer* buffer,
    _In_ int               pageStart,
    _In_ int               pageCount);

#endif //!__SHM_PRIVATE_H__
