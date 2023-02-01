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

#ifndef __MS_PRIVATE_H__
#define __MS_PRIVATE_H__

#include <ds/list.h>
#include <memoryspace.h>
#include <mutex.h>

struct MSAllocation {
    element_t            Header;
    MemorySpace_t*       MemorySpace;
    uuid_t               SHMTag;
    vaddr_t              Address;
    size_t               Length;
    unsigned int         Flags;
    int                  References;
    struct MSAllocation* CloneOf;
};

struct MSContext {
    DynamicMemoryPool_t Heap;
    list_t              Allocations;
    uintptr_t           SignalHandler;
    Mutex_t             SyncObject;
};

/**
 * @brief Returns a new instance of a memory space (shared) context.
 * @return An allocated and instantiated memory space context.
 */
struct MSContext*
MSContextNew(void);

/**
 * @brief Destroys and frees any resources associated with the memory space context.
 * @param context The memory space context to clean up.
 */
void
MSContextDelete(
        _In_ struct MSContext* context);

/**
 * @brief Registers a new allocation with the given context.
 * @param context
 * @param allocation
 */
void
MSContextAddAllocation(
        _In_ struct MSContext*    context,
        _In_ struct MSAllocation* allocation);

/**
 * @brief
 * @param memorySpace
 * @param address
 * @param length
 * @param flags
 * @return
 */
oserr_t
MSAllocationCreate(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        address,
        _In_ size_t         length,
        _In_ unsigned int   flags);

/**
 * @brief
 * @param context
 * @param address
 * @param size
 * @param clonedFrom
 * @return
 */
oserr_t
MSAllocationFree(
        _In_  struct MSContext*     context,
        _In_  vaddr_t*              address,
        _In_  size_t*               size,
        _Out_ struct MSAllocation** clonedFrom);

/**
 * @brief Looks up an existing allocation.
 * @param context
 * @param address
 * @return
 */
struct MSAllocation*
MSAllocationLookup(
        _In_ struct MSContext* context,
        _In_ vaddr_t           address);

/**
 * Looks up and increases the reference count by one if the allocation
 * was found.
 * @param context
 * @param address
 * @return
 */
struct MSAllocation*
MSAllocationAcquire(
        _In_ struct MSContext* context,
        _In_ vaddr_t           address);

/**
 * @brief
 * @param context
 * @param address
 * @param link
 * @return
 */
oserr_t
MSAllocationLink(
        _In_ struct MSContext*    context,
        _In_ vaddr_t              address,
        _In_ struct MSAllocation* link);

/**
 * @brief
 * @param flags
 * @return
 */
MemorySpace_t*
MemorySpaceNew(
        _In_ unsigned int flags);

/**
 * @brief
 * @param memorySpace
 */
void
MemorySpaceDelete(
        _In_ MemorySpace_t* memorySpace);

/**
 * @brief Synchronizes a region of memory with all active memory spaces across CPU cores.
 * @param memorySpace
 * @param address
 * @param size
 */
extern void
MSSync(
        _In_ MemorySpace_t* memorySpace,
        _In_ uintptr_t      address,
        _In_ size_t         size);

#endif //!__MS_PRIVATE_H__
