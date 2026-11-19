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
 *
 * IOMMU backend interface. Defines the unit limits, hardware callbacks, and
 * address-space setup functions shared between generic DMA code and a platform
 * IOMMU driver.
 */

#ifndef __DMA_BACKEND_H__
#define __DMA_BACKEND_H__

#include <dma/address.h>

#include "iova.h"

// Private to DMA. An IOMMU is hardware that sits between devices and RAM. When
// a device accesses memory using a device address (an I/O virtual address, or
// IOVA), the IOMMU translates it into a physical RAM address and checks read or
// write permissions. One IOMMU unit can manage several separate address spaces.
// Each device tags its memory traffic with a hardware ID, such as a PCI bus,
// device, and function number, so the unit knows which address space to apply.
DECL_STRUCT(DMAIOMMUUnit);

/**
 * @brief Fixed limits that a backend driver supplies before creating an IOMMU unit.
 *
 * Granularity is the size of one granule, the smallest memory block the IOMMU
 * can map (such as a 4 KiB page). It must be a power of two and at least 4096
 * bytes. AddressLimit is the highest device address (IOVA) the unit can
 * translate. PhysicalLimit is the highest physical RAM address it can reach.
 * Both limits are inclusive, and each must leave room for at least one full
 * granule starting at address zero.
 *
 * CompletionTimeoutNs is a wait budget in nanoseconds and cannot be zero. Every
 * hardware wait inside a single mapping or hardware ID connection call shares
 * this budget. It does not cap how long a thread waits to acquire the unit lock.
 */
typedef struct DMAIOMMUUnitConfig {
    uint64_t Granularity;
    uint64_t AddressLimit;
    uint64_t PhysicalLimit;
    uint64_t CompletionTimeoutNs;
} DMAIOMMUUnitConfig_t;

/**
 * @brief Describes one device-address mapping and the physical memory behind it.
 *
 * Generic DMA code leaves this struct and its copy of Extents unchanged until
 * DestroyMapping finishes. Extents is an array of physical memory chunks in
 * buffer order. Each chunk covers whole granules, and their lengths add up to
 * Length with no gaps. Direction states whether the device may read, write, or
 * both, as defined by DMADirection. Devices are never given execute permission.
 */
typedef struct DMAIOMMUMappingConfig {
    uint64_t          DeviceAddress;
    uint64_t          Length;
    enum DMADirection Direction;
    const SHMSG_t*    Extents;
    int               ExtentCount;
} DMAIOMMUMappingConfig_t;

// Why the IOMMU refused an access. The first four kinds describe an access a
// device sent. NoRoute and ReportsLost carry no address space to search.
enum DMAIOMMUFaultKind {
    DMAIOMMUFaultNoMapping,    // The device address has no installed mapping.
    DMAIOMMUFaultPermission,   // A mapping exists, but not for this direction.
    DMAIOMMUFaultAddressRange, // The address is above what the unit can translate.
    DMAIOMMUFaultOther,        // Another refused access. Code holds the details.
    DMAIOMMUFaultNoRoute,      // The hardware ID is connected to no address space.
    DMAIOMMUFaultReportsLost   // The unit dropped an unknown number of reports.
};

// Set when DeviceAddress holds the address the device used.
#define DMA_IOMMU_FAULT_ADDRESS 0x1U
// Set when the unit reported the direction of the refused access.
#define DMA_IOMMU_FAULT_READ    0x2U
#define DMA_IOMMU_FAULT_WRITE   0x4U

/**
 * @brief One access that the IOMMU refused, as reported by a backend.
 *
 * HardwareID uses the same unit-local numbering as PrepareHardwareIDs. It is
 * valid for every kind except DMAIOMMUFaultReportsLost, where it is zero.
 * Flags is a mask of DMA_IOMMU_FAULT_* bits. DeviceAddress is only valid when
 * DMA_IOMMU_FAULT_ADDRESS is set. Code is the raw event or status value of the
 * backend and is only used in the log. The record holds no pointers, so it can
 * be copied and kept after the unit mutex is released.
 */
typedef struct DMAIOMMUFault {
    enum DMAIOMMUFaultKind Kind;
    uint32_t               Flags;
    uint32_t               HardwareID;
    uint32_t               Code;
    uint64_t               DeviceAddress;
} DMAIOMMUFault_t;

/**
 * @brief Hardware callbacks that a backend driver provides for one IOMMU unit.
 *
 * Generic DMA code calls these one at a time while holding the unit lock,
 * always from thread context so callbacks can sleep if needed. Every callback
 * is required.
 *
 * A callback must never call back into shared-memory (SHM) code, attachment
 * management, the IOVA allocator, or other generic DMA functions, because doing
 * so can re-enter the unit lock or invert lock order and deadlock. Calling
 * DMAIOMMUUnitFail on the same unit is safe, because it takes no lock. The
 * caller keeps the physical memory pages valid until RemoveMapping finishes.
 * A callback never frees those pages or releases IOVA ranges on its own.
 */
typedef struct DMAIOMMUBackendOps {
    // Allocate the top-level page table (translation root) for one address
    // space. No device hardware ID is routed to it yet. On error, undo any
    // partial work and leave *spaceOut as NULL.
    oserr_t (*CreateSpace)(
        _In_    void*  unitContext,
        _InOut_ void** spaceOut);

    // Free the address space using software cleanup only. Must not allocate
    // memory. Every mapping and hardware ID connection must already be gone.
    // Intermediate page-table pages shared across mappings are freed here.
    void (*DestroySpace)(
        _In_ void* unitContext,
        _In_ void* space);

    // Allocate everything the mapping needs (page-table pages, command entries,
    // and completion tracking) without making the mapping visible to any device.
    // On error, free temporary work and leave *mappingOut as NULL. The backend
    // may keep pointers into config, because config stays valid until
    // DestroyMapping. Reject unsupported permissions here. Look up shared
    // intermediate table pages in InstallMapping instead of here, because two
    // prepared mappings can be installed in any order. Once a shared table page
    // is visible to the hardware, keep it until DestroySpace.
    oserr_t (*PrepareMapping)(
        _In_    void*                          space,
        _In_    const DMAIOMMUMappingConfig_t* config,
        _InOut_ void**                         mappingOut);

    // Install a prepared mapping. Called at most once per mapping. Returning
    // OS_EOK promises the full mapping is visible with the exact permissions
    // requested. On any error, keep a record inside the mapping of entries
    // already written, commands already submitted, and completions still
    // pending, so RemoveMapping can undo them. Must not allocate memory.
    oserr_t (*InstallMapping)(
        _In_ void*    space,
        _In_ void*    mapping,
        _In_ uint64_t timeoutNs);

    // Remove every entry installed so far and wait until the hardware confirms
    // it has dropped its cached address translations (TLB entries). Retries
    // pick up from the saved progress, including install commands that were
    // still pending. Returning OS_EOK promises that no translation or pending
    // command can still reach the old RAM, and that no hardware work will touch
    // the mapping, config, or extent copy afterward. The caller must stop the
    // device before calling this. Must not allocate memory.
    oserr_t (*RemoveMapping)(
        _In_ void*    space,
        _In_ void*    mapping,
        _In_ uint64_t timeoutNs);

    // Free the mapping record using software cleanup only. Cannot fail and
    // must not allocate memory. Accepts either a mapping that was only prepared
    // or one whose RemoveMapping succeeded. Afterward the backend must hold no
    // pointer into the mapping's storage. Private page-table pages that no
    // other entry uses are freed here. Shared table pages stay with the space.
    void (*DestroyMapping)(
        _In_ void* space,
        _In_ void* mapping);

    // Reserve the full set of unit-local hardware IDs and allocate all
    // connection tracking data without changing any hardware routes. Copy the
    // IDs or rely on the caller's list staying valid until DestroyHardwareIDs.
    // On error, undo partial work and leave *connectionOut as NULL. Two spaces
    // on the same unit cannot reserve the same hardware ID at once, even across
    // different DMA managers. Keep reservations until DestroyHardwareIDs.
    oserr_t (*PrepareHardwareIDs)(
        _In_    void*           space,
        _In_    const uint32_t* hardwareIDs,
        _In_    uint32_t        hardwareIDCount,
        _InOut_ void**          connectionOut);

    // Route every reserved hardware ID to this address space's top-level table.
    // Called once while the devices are idle with no DMA transfers running. On
    // any error, save partial table and command progress so
    // DisconnectHardwareIDs can clean up later. Must not allocate memory.
    oserr_t (*ConnectHardwareIDs)(
        _In_ void*    space,
        _In_ void*    connection,
        _In_ uint64_t timeoutNs);

    // Disconnect a complete or partial hardware ID connection. Retries pick up
    // from saved progress. Returning OS_EOK promises that no reserved hardware
    // ID, cached route, pending command, or in-flight page-table lookup can
    // still reach the old top-level table. The caller must stop all DMA
    // transfers on the devices first. Must not allocate or free data pages.
    oserr_t (*DisconnectHardwareIDs)(
        _In_ void*    space,
        _In_ void*    connection,
        _In_ uint64_t timeoutNs);

    // Release hardware ID reservations and connection tracking data after
    // preparation alone or after a completed disconnect. Software cleanup only.
    // Cannot fail, allocate memory, or submit hardware commands.
    void (*DestroyHardwareIDs)(
        _In_ void* space,
        _In_ void* connection);
} DMAIOMMUBackendOps_t;

/**
 * @brief Creates a new IOMMU unit from a backend configuration and callbacks.
 *
 * No hardware is touched and no device routing changes. Keep unitContext and
 * the resources it points to valid until unit destruction succeeds. Pass NULL
 * for unitContext if the backend needs no context. Both config and ops are
 * copied, so the caller can free them as soon as this returns.
 *
 * @param config      Hardware limits and wait budget for the unit.
 * @param ops         Backend callbacks. Every callback pointer must be non-NULL.
 * @param unitContext Opaque pointer passed to CreateSpace and DestroySpace.
 * @param unitOut     Pointer to a NULL slot that receives the new unit. Unchanged on error.
 * @return OS_EOK on success, OS_EINVALPARAMS for missing inputs, missing
 *         callbacks, or invalid limits, OS_EBUSY if *unitOut is not NULL, or
 *         OS_EOOM if allocation fails.
 */
KERNELABI oserr_t KERNELAPI
DMAIOMMUUnitCreate(
    _In_    const DMAIOMMUUnitConfig_t* config,
    _In_    const DMAIOMMUBackendOps_t* ops,
    _In_    void*                      unitContext,
    _InOut_ DMAIOMMUUnit_t**            unitOut);

/**
 * @brief Permanently halts new work on every address space this IOMMU unit serves.
 *
 * Records the first failure reason that is not OS_EOK and logs it. Runs in
 * thread context without taking the unit lock, so a backend callback that
 * already holds the unit lock can call it safely. A backend must call this
 * before returning any error that means the IOMMU lost command ordering or can
 * no longer report completions reliably across mappings. A single command
 * timeout on its own does not need to fail the unit.
 *
 * This does not stop any physical device, and a failed unit cannot be reset.
 * Keep a reference to the unit or one of its address spaces alive during the call.
 *
 * @param unit   Live unit that has not been destroyed. NULL logs a programming error.
 * @param reason Error explaining why the unit failed. OS_EOK logs a programming error.
 */
KERNELABI void KERNELAPI
DMAIOMMUUnitFail(
    _In_ DMAIOMMUUnit_t* unit,
    _In_ oserr_t         reason);

/**
 * @brief Takes the IOMMU unit's mutex from backend code outside the DMA callbacks.
 *
 * A backend driver can call this during driver-specific maintenance, fault
 * handling, or teardown so it does not race with CreateSpace, PrepareMapping,
 * or other DMA callbacks. The standard callbacks in DMAIOMMUBackendOps_t already
 * hold this mutex and must never call DMAIOMMUUnitLock again.
 *
 * Requires a live, non-NULL unit in thread context, and the caller must not
 * already hold the mutex. Failed units still allow locking so diagnostics and
 * cleanup can serialize safely. While locked, only touch backend driver state
 * and the lock-free DMAIOMMUUnitCheck or DMAIOMMUUnitFail helpers. Never call
 * into generic DMA, manager, recovery-group, or SHM code.
 *
 * @param unit Live IOMMU unit to lock.
 */
KERNELABI void KERNELAPI
DMAIOMMUUnitLock(
    _In_ DMAIOMMUUnit_t* unit);

/**
 * @brief Releases the unit mutex acquired by DMAIOMMUUnitLock.
 *
 * @param unit Same IOMMU unit passed to DMAIOMMUUnitLock.
 */
KERNELABI void KERNELAPI
DMAIOMMUUnitUnlock(
    _In_ DMAIOMMUUnit_t* unit);

/**
 * @brief Reads the global counter of IOMMU unit failures without locking.
 *
 * Every time an IOMMU unit records its first failure, this counter increases
 * by 1. DMA managers compare this value against their last checked sequence to
 * skip scanning all their domains when no new unit has failed. If the counter
 * reaches UINT_MAX, it stays pinned there instead of wrapping, forcing managers
 * to scan on every check.
 *
 * @return Current global failure sequence number.
 */
KERNELABI unsigned int KERNELAPI
DMAIOMMUGetFailureSequence(void);

/**
 * @brief Checks whether an IOMMU unit has failed, without taking its mutex.
 *
 * Safe to call while holding spinlocks or IRQ locks. Keep a reference to the
 * unit alive for the duration of the call. Returning OS_EOK only reports that
 * no failure has been recorded yet. Callers still need the normal binding and
 * recovery-group checks before starting DMA. This also reports failures logged
 * by generic DMA code through DMAIOMMUUnitFail.
 *
 * @param unit IOMMU unit to inspect.
 * @return OS_EOK if healthy, OS_ECANCELLED if the unit failed, or
 *         OS_EINVALPARAMS if unit is NULL.
 */
KERNELABI oserr_t KERNELAPI
DMAIOMMUUnitCheck(
    _In_ DMAIOMMUUnit_t* unit);

/**
 * @brief Frees an IOMMU unit once no address space uses it.
 *
 * The backend's own hardware resources stay with the caller that created them.
 * Before calling this, stop all new calls into the unit and wait for any running
 * calls to finish. Checking that the unit's space count is zero is not enough
 * on its own, because a destructor on another thread may have decremented the
 * count just before returning.
 *
 * @param unit Address of the unit pointer. Cleared to NULL only on success.
 *             Does nothing if unit or *unit is NULL.
 * @return OS_EOK on success, or OS_EBUSY while any address space remains
 *         (including one whose cleanup did not finish).
 */
KERNELABI oserr_t KERNELAPI
DMAIOMMUUnitDestroy(
    _InOut_ DMAIOMMUUnit_t** unit);

/**
 * @brief Creates a translated address space with an IOVA allocator and a top-level table.
 *
 * All devices in a domain share one address space. The space holds the device
 * addresses (IOVAs) those devices can use and the page tables that translate them
 * into physical memory. No device hardware ID is routed to the new table yet.
 *
 * The granule size in config must match the unit's granularity, and its Bounds
 * must fit within AddressLimit. Reserved IOVA ranges follow the rules of
 * DMAIOVAAllocatorCreate and are copied during this call. An attachment added
 * later must keep a reference to this space until its hardware ID is
 * disconnected and the hardware has finished all table lookups.
 *
 * @param unit     Live unit that stays valid throughout this call.
 * @param config   IOVA allocator settings, kept unchanged until this returns.
 * @param spaceOut Address of a NULL pointer that receives the space with one
 *                 reference on success. Unchanged on error.
 * @return OS_EOK on success, OS_EINVALPARAMS for invalid inputs, OS_EBUSY if
 *         *spaceOut is not NULL, OS_ECANCELLED if the unit has failed,
 *         OS_EOVERFLOW for a count or size overflow, or an allocator or backend
 *         error. If the backend fails while the unit also fails, the backend
 *         error is returned.
 */
KERNELABI oserr_t KERNELAPI
DMAAddressSpaceCreateTranslated(
    _In_    DMAIOMMUUnit_t*          unit,
    _In_    const DMAIOVAConfig_t*   config,
    _InOut_ struct DMAAddressSpace** spaceOut);

#endif //!__DMA_BACKEND_H__
