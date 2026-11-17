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

#ifndef __DMA_BACKEND_H__
#define __DMA_BACKEND_H__

#include <dma/address.h>

#include "iova.h"

// Private to DMA. A unit is one IOMMU, the hardware that translates the
// addresses a device uses into physical addresses. One unit can serve several
// independent address spaces. A requester is the ID the unit sees on the memory
// requests of a device, for example a PCI bus, device and function number.
DECL_STRUCT(DMAIOMMUUnit);

/**
 * @brief Fixed limits that the backend picks before it creates a unit.
 *
 * Granularity is the size of one granule, the smallest block the IOMMU can map.
 * It must be a power of two and at least 4096 bytes. AddressLimit is the
 * highest device address (IOVA) the unit can translate. PhysicalLimit is the
 * highest physical address it can reach. Both limits are inclusive, and each
 * must leave room for one whole granule starting at zero.
 *
 * CompletionTimeoutNs is a time budget in nanoseconds and must not be zero.
 * All hardware waits inside one InstallMapping or RemoveMapping call share it.
 * It does not limit how long a thread waits for the unit mutex.
 */
typedef struct DMAIOMMUUnitConfig {
    uint64_t Granularity;
    uint64_t AddressLimit;
    uint64_t PhysicalLimit;
    uint64_t CompletionTimeoutNs;
} DMAIOMMUUnitConfig_t;

/**
 * @brief The device address of one mapping and the physical memory behind it.
 *
 * The generic DMA code does not change this struct, or its own copy of the
 * extents, until DestroyMapping has finished. Extents cover whole granules in
 * buffer order. Their lengths add up to Length with no padding. Direction says
 * whether the device may read, write or both, as defined by DMADirection. The
 * device is never allowed to execute from the memory.
 */
typedef struct DMAIOMMUMappingConfig {
    uint64_t          DeviceAddress;
    uint64_t          Length;
    enum DMADirection Direction;
    const SHMSG_t*    Extents;
    int               ExtentCount;
} DMAIOMMUMappingConfig_t;

/**
 * @brief The callbacks a backend provides for one unit.
 *
 * The generic DMA code calls them one at a time while holding the unit mutex,
 * always from thread context. Every callback is required. A callback must not
 * call into SHM, attachment management, the IOVA allocator or any other generic
 * DMA operation, because those could take the unit mutex again or other locks
 * and deadlock. Calling DMAIOMMUUnitFail on the same unit is fine, because it
 * takes no mutex. The caller keeps the physical data pages valid. A callback
 * never frees those pages or IOVA ranges.
 */
typedef struct DMAIOMMUBackendOps {
    // Allocate a new translation root, the top-level table of one address
    // space. No requester can walk it yet. On error, undo any temporary work
    // and leave the output (which starts out NULL) unchanged.
    oserr_t (*CreateSpace)(
        _In_    void*  unitContext,
        _InOut_ void** spaceOut);

    // Free the space using software only. This must not allocate. All mappings
    // and requester references must be gone before it runs. Shared table pages
    // are freed here.
    void (*DestroySpace)(
        _In_ void* unitContext,
        _In_ void* space);

    // Allocate everything the mapping needs (table pages, commands and
    // completion tracking) without making any translation visible to the
    // device. On error, free the temporary work and leave mappingOut NULL. The
    // record may keep pointers into config. Reject unsupported permissions here.
    // Look up the shared table pages in InstallMapping instead, because
    // prepared mappings can be installed in any order. Once a shared page is
    // visible to the hardware, it stays until DestroySpace.
    oserr_t (*PrepareMapping)(
        _In_    void*                          space,
        _In_    const DMAIOMMUMappingConfig_t* config,
        _InOut_ void**                         mappingOut);

    // Called once per mapping. OS_EOK means the whole mapping is visible with
    // exactly the requested permissions. On every error, keep a record in the
    // mapping of the entries already changed, the commands already submitted
    // and the completion tokens still pending. Do not allocate.
    oserr_t (*InstallMapping)(
        _In_ void*    space,
        _In_ void*    mapping,
        _In_ uint64_t timeoutNs);

    // Undo the entries installed so far and wait until the hardware confirms
    // that it dropped its cached translations. A retry continues the saved
    // work, including install commands that are still pending. OS_EOK means
    // no translation or pending command can still reach the old memory, and no
    // command or completion can touch the mapping, its config or its extent
    // copy later. Stopping the device is the caller's job and must happen
    // before this call. Do not allocate.
    oserr_t (*RemoveMapping)(
        _In_ void*    space,
        _In_ void*    mapping,
        _In_ uint64_t timeoutNs);

    // Free the record using software only. This cannot fail and must not
    // allocate. It accepts a record that was only prepared, or one whose
    // RemoveMapping has finished. Afterwards the backend must hold no pointer
    // to the record's storage. Private table pages that no entry uses are
    // freed here. Shared pages stay with the space.
    void (*DestroyMapping)(
        _In_ void* space,
        _In_ void* mapping);
} DMAIOMMUBackendOps_t;

/**
 * @brief Copy a backend configuration and set up the unit's mutex and failure state.
 *
 * No hardware is touched and no device routing changes. Keep unitContext and
 * the resources it points to valid until unit destruction succeeds. A NULL
 * context is allowed for a backend that does not need one. The config and the
 * callbacks are copied, so the caller can free them after this returns.
 *
 * @param unitOut Address of a pointer that must be NULL. Unchanged on error.
 * @return OS_EOK, OS_EINVALPARAMS for a missing input or callback or invalid
 *         limits, OS_EBUSY if *unitOut is not NULL, or OS_EOOM if allocation
 *         failed.
 */
KERNELABI oserr_t KERNELAPI
DMAIOMMUUnitCreate(
    _In_    const DMAIOMMUUnitConfig_t* config,
    _In_    const DMAIOMMUBackendOps_t* ops,
    _In_    void*                      unitContext,
    _InOut_ DMAIOMMUUnit_t**            unitOut);

/**
 * @brief Permanently stop new work on every address space this unit serves.
 *
 * Records the first reason that is not OS_EOK and logs it. This runs in thread
 * context and takes no mutex, so a callback that already holds the unit mutex
 * can call it. A backend must call it before it returns an error that means
 * the ordering or completion state shared by its mappings can no longer be
 * trusted. A single pending command that times out does not need to fail the
 * unit. This does not stop any device, and a failed unit cannot be reset. Keep
 * a reference to the unit or one of its spaces alive during the call.
 *
 * @param unit Unit that has not been destroyed. NULL is a logged programming error.
 * @param reason Why the unit failed. OS_EOK is a logged programming error.
 */
KERNELABI void KERNELAPI
DMAIOMMUUnitFail(
    _In_ DMAIOMMUUnit_t* unit,
    _In_ oserr_t         reason);

/**
 * @brief Free a unit that no space uses. The backend resources stay with its creator.
 *
 * First stop new calls into the unit and wait for running calls to return. A
 * space count of zero is not enough on its own, because a destructor on another
 * thread may have lowered the count without returning yet.
 *
 * @param unit Set to NULL only on success. Nothing happens if unit or *unit is NULL.
 * @return OS_EOK, or OS_EBUSY while any space remains. That includes a space
 *         whose cleanup did not finish.
 */
KERNELABI oserr_t KERNELAPI
DMAIOMMUUnitDestroy(
    _InOut_ DMAIOMMUUnit_t** unit);

/**
 * @brief Create an address space for a domain, with an IOVA allocator and a new translation root.
 *
 * A domain is a group of devices that share one address space. The space holds
 * the device addresses (IOVAs) the domain may use and the tables that map them
 * to physical memory. The new root is not visible to any device yet.
 *
 * The granularity in config must equal the unit granularity, and its Bounds
 * must fit within AddressLimit. Reserved ranges are handled as described for
 * DMAIOVAAllocatorCreate and are copied during this call. No requester is
 * connected here. A requester attachment added later must hold a space
 * reference until the requester is disconnected and the hardware has finished
 * any reads of the tables.
 *
 * @param unit Unit that has not been destroyed. The caller keeps it alive for the whole call.
 * @param config Left unchanged by the caller until this returns.
 * @param spaceOut Address of a pointer that must be NULL. On success it receives
 *                 the space with one reference.
 * @return OS_EOK, OS_EINVALPARAMS for invalid inputs, OS_EBUSY if *spaceOut is
 *         not NULL, OS_ECANCELLED for a failed unit, OS_EOVERFLOW for a count or
 *         size overflow, or an allocator or backend error. The output is
 *         unchanged on error. If the backend fails while the unit also fails,
 *         the backend error is returned.
 */
KERNELABI oserr_t KERNELAPI
DMAAddressSpaceCreateTranslated(
    _In_    DMAIOMMUUnit_t*          unit,
    _In_    const DMAIOVAConfig_t*   config,
    _InOut_ struct DMAAddressSpace** spaceOut);

#endif //!__DMA_BACKEND_H__
