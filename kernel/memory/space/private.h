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

#include <assert.h>
#include <handles/global.h>
#include <ds/list.h>
#include <ds/bitmap.h>
#include <memoryspace.h>
#include <mutex.h>
#include <shm/view.h>

/**
 * @brief Restricted mapping limits and logical offset into the backing buffer.
 * The allocation holds one independent reference to View until all its pages
 * have been unmapped. Offset is the logical backing offset of the allocation's
 * first virtual page.
 */
struct MSRestrictedMapping {
    uuid_t     ReferenceID;
    SHMView_t* View;
    size_t     Offset;
    bool       CleanupPending;
};

/**
 * @brief Tracked virtual allocation, released-page bitmap and clone references.
 * SHM records immutable limits and an independent reference for restricted maps.
 */
struct MSAllocation {
    element_t                  Header;
    MemorySpace_t*             MemorySpace;
    uuid_t                     SHMTag;
    vaddr_t                    Address;
    size_t                     Length;
    bitmap_t                   Pages;
    unsigned int               Flags;
    int                        References;
    struct MSAllocation*       CloneOf;
    struct MSRestrictedMapping SHM;
};

/**
 * @brief Resources shared by all memory spaces in a group.
 * This is the OS concept of a process context.
 */
struct MSContext {
    MSContextSecurity_t Security;
    DynamicMemoryPool_t Heap;
    list_t              Allocations;
    uintptr_t           SignalHandler;
    Mutex_t             SyncObject;
    Mutex_t             UserCopyLock;
};

/**
 * @brief Create a memory-space group with one global reference.
 * No process grant is added. Release the reference with ReleaseGlobalHandle.
 * @return The group's global handle ID, or UUID_INVALID if allocation fails.
 */
uuid_t
MSContextNew(void);

/**
 * @brief Look up the context shared by the memory-space group.
 * The memory space must remain valid during the call.
 *
 * @param memorySpace Memory space with a live global reference to its group.
 * @return The shared context, or NULL for a kernel memory space without a group.
 */
static inline struct MSContext*
__MSContextGet(
    _In_ MemorySpace_t* memorySpace)
{
    struct MSContext* context;

    if (memorySpace->GroupGlobalHandleID == UUID_INVALID) {
        return NULL;
    }

    context = LookupGlobalHandle(memorySpace->GroupGlobalHandleID, HandleTypeMSContext);
    assert(context != NULL);
    return context;
}

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
 * @param memorySpace Memory space in which the allocation was made.
 * @param address Allocated address. Must be aligned to a page boundary.
 * @param length Allocation length, rounded up to the nearest page size.
 * @param flags
 * @return
 */
oserr_t
MSAllocationCreate(
    _In_ MemorySpace_t* memorySpace,
    _In_ uuid_t         shmTag,
    _In_ vaddr_t        address,
    _In_ size_t         length,
    _In_ unsigned int   flags);

/**
 * @brief Free all or part of an existing allocation.
 * @param context Memory-space context in which the allocation was made.
 * @param address First address to free. Must be aligned to a page boundary.
 * @param length Length to free, rounded up to the nearest page size.
 * @param clonedFrom Receives the initial allocation if this is a clone.
 *                   The caller must supply a pointer.
 * @return OS_EOK if the allocation and all associated resources were freed.
 *         OS_EINCOMPLETE if only part of the allocation was freed.
 *         OS_ELINKS if linked allocations prevented the free.
 */
oserr_t
MSAllocationFree(
    _In_  struct MSContext*     context,
    _In_  vaddr_t               address,
    _In_  size_t                length,
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
 * Look up an allocation and increase its reference count by one if found.
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
 * @param allocation
 * @return
 */
oserr_t
MSAllocationRelease(
    _In_ struct MSContext*    context,
    _In_ struct MSAllocation* allocation);

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

/**
 * @brief Find the restricted allocation that overlaps a virtual byte range.
 * The caller must hold UserCopyLock during this call and subsequent mapping
 * changes. A range touching restricted memory must fit within one live
 * allocation, including its pages.
 * @param context Current allocation context, or NULL for kernel mappings.
 * @param address First virtual byte.
 * @param length Number of bytes. Must be nonzero.
 * @param allowFreedPages Allow holes when unmapping a previously split range.
 * @param allocationOut Receives the restricted allocation, or NULL.
 * @return OS_EOK, OS_EINVALPARAMS or OS_ENOTSUPPORTED for a mixed range.
 */
oserr_t
MSAllocationFindSHM(
    _In_  struct MSContext*     context,
    _In_  vaddr_t               address,
    _In_  size_t                length,
    _In_  bool                 allowFreedPages,
    _Out_ struct MSAllocation** allocationOut);

/**
 * @brief Allocate and register process memory with its restricted reference, if any.
 * For restricted mappings, the caller must validate the view policy and hold
 * UserCopyLock until mapping or rollback completes.
 *
 * @param memorySpace Process memory space with a valid context.
 * @param options Mapping policy and optional restricted view reference.
 * @param length Complete virtual allocation size, including any stack guard.
 * @return Allocated address, or zero on failure.
 */
vaddr_t
MSMappingAllocate(
    _In_ MemorySpace_t*                memorySpace,
    _In_ struct MemorySpaceMapOptions* options,
    _In_ size_t                        length);

/**
 * @brief Roll back a restricted mapping and track unfinished cleanup for retry.
 * The caller must hold UserCopyLock from mapping creation until this call
 * completes. Supply the complete allocation range before discarding its address.
 *
 * @param memorySpace Memory space containing the restricted allocation.
 * @param address First virtual byte of the allocation.
 * @param length Complete allocation length in bytes.
 * @return OS_EOK or the unmap error. Failed allocations stay marked for cleanup.
 */
oserr_t
MSMappingRollback(
    _In_ MemorySpace_t* memorySpace,
    _In_ vaddr_t        address,
    _In_ size_t         length);

/**
 * @brief Retry unfinished restricted mapping cleanup in a memory-space context.
 * The caller must hold the context's UserCopyLock for the complete call.
 *
 * @param memorySpace Memory space containing the pending allocations.
 * @param viewID Reference identifying the restricted mappings to clean.
 * @return OS_EOK when no cleanup remains, or the unmap error.
 */
oserr_t
MSMappingCleanup(
    _In_ MemorySpace_t* memorySpace,
    _In_ uuid_t         viewID);

/**
 * @brief Derive and validate clone policy from a restricted source allocation.
 * The source range must have passed MSAllocationFindSHM. The caller must hold
 * the source context's UserCopyLock until the clone operation finishes.
 *
 * @param allocation Restricted source allocation.
 * @param address First source byte.
 * @param length Requested clone length.
 * @param memoryFlags Requested access flags.
 * @param placementFlags Requested destination placement.
 * @param options Receives mapping policy for complete pages, excluding physical pages.
 * @return OS_EOK or a range, placement or permission error.
 */
oserr_t
MSMappingCloneOptions(
    _In_  struct MSAllocation*          allocation,
    _In_  vaddr_t                       address,
    _In_  size_t                        length,
    _In_  unsigned int                  memoryFlags,
    _In_  unsigned int                  placementFlags,
    _Out_ struct MemorySpaceMapOptions* options);

/**
 * @brief Map while the caller holds the context's UserCopyLock.
 * @param memorySpace Target memory space.
 * @param options Validated mapping request.
 * @param mappingOut Receives the mapped address.
 * @return OS_EOK or a mapping error.
 */
oserr_t
MSMap(
    _In_  MemorySpace_t*                memorySpace,
    _In_  struct MemorySpaceMapOptions* options,
    _Out_ vaddr_t*                      mappingOut);

/**
 * @brief Unmap while the caller holds the context's UserCopyLock.
 */
oserr_t
MSUnmap(
    _In_ MemorySpace_t* memorySpace,
    _In_ vaddr_t        address,
    _In_ size_t         length);

#endif //!__MS_PRIVATE_H__
