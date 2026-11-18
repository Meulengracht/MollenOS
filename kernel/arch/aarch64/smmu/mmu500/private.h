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
 * MMU-500 internal records and module boundaries.
 * All mutable fields below are protected by the generic DMA unit mutex, except
 * the atomic Common.State field. Generic DMA backend callbacks enter with that
 * mutex already held.
 */

#ifndef __ARM_SMMU500_PRIVATE_H__
#define __ARM_SMMU500_PRIVATE_H__

#include "smmu500.h"
#include "../deadline.h"
#include "../memory.h"
#include "../stage2.h"
#include "../stage2-table.h"
#include "../unit-common.h"
#include "registers.h"
#include <ds/list.h>
#include <stdatomic.h>

// Upper bound on context banks and stream match slots per controller, matching
// the fixed 128-slot limits in the SMMU description and MMU-500 architecture.
#define SMMU500_MAX_RESOURCES 128U

// Tracks how far hardware initialization progressed during SMMU500UnitStart.
// Teardown checks this to skip register writes if startup never touched hardware.
enum SMMU500StartProgress {
    SMMU500NoWrites,         // Probe only. No control register has been written.
    SMMU500Initializing,     // Global controls, stream slots, or banks are being reset.
    SMMU500StartupSync,      // Initial global TLB sync was submitted and is draining.
    SMMU500StartupComplete   // Hardware reset and initial TLB sync finished cleanly.
};

// Resumable shutdown stages for SMMU500UnitDestroy when waits time out.
enum SMMU500CloseProgress {
    SMMU500CloseInitial,     // Initial entry: switch global CR0 to fault mode.
    SMMU500CloseSlots,       // Set each stream slot (up to CloseIndex) to fault.
    SMMU500CloseBanks,       // Disable each context bank (up to CloseIndex).
    SMMU500CloseSync,        // Wait for the final global TLB sync to finish.
    SMMU500CloseMemory       // Hardware stopped. Free the control memory pool next.
};

// Scope for a global TLB invalidation and sync request.
enum SMMU500SyncScope {
    SMMU500SyncVMID,         // Invalidate TLB entries tagged with one space's VMID.
    SMMU500SyncAll           // Invalidate all Non-secure non-Hyp TLB entries.
};

/**
 * @brief Saved state for one TLB invalidation and global synchronization request.
 *
 * Ticket 0 means the invalidation has not been submitted to hardware yet. Once
 * assigned, Ticket stays fixed so if another operation finishes the unit's
 * active sync before this caller retries, the caller sees its ticket is already
 * done instead of waiting on or resubmitting newer work.
 */
typedef struct SMMU500Sync {
    enum SMMU500SyncScope Scope;
    uint32_t              VMID;
    uint64_t              Ticket;
} SMMU500Sync_t;

DECL_STRUCT(SMMU500Space);

// Tracks whether a hardware context bank slot is currently assigned to a space.
typedef struct SMMU500Context {
    SMMU500Space_t* Space;
} SMMU500Context_t;

// Tracks one hardware stream match slot (SMR/S2CR pair) reserved by a space.
typedef struct SMMU500StreamSlot {
    bool     Reserved;
    uint32_t SID;
} SMMU500StreamSlot_t;

/**
 * @brief Full driver state for one MMU-500 controller unit.
 *
 * Embeds ArmSMMUUnitCommon_t for lifecycle gating, MMIO mapping, and the
 * control memory pool. Hardware geometry fields (PageShift through OutputEncoding)
 * are populated once during probing in SMMU500UnitCreate and stay constant.
 * SyncsSubmitted and SyncsCompleted serialize global TLB sync tickets so every
 * caller can tell whether its saved ticket has already drained.
 */
struct SMMU500Unit {
    ArmSMMUUnitCommon_t            Common;          // Common SMMU lifecycle and control memory pool.
    enum SMMU500StartProgress      StartProgress;   // Furthest point reached during hardware startup.
    enum SMMU500CloseProgress      CloseProgress;   // Next shutdown step to run or resume on retry.
    uint32_t                       CloseIndex;      // Next slot or bank index during resumable close.
    uint32_t                       PageShift;       // Register page size shift (12 for 4 KiB, 16 for 64 KiB).
    uint32_t                       ContextBase;     // Byte offset from RegisterBase to context bank 0.
    uint32_t                       ContextCount;    // Total hardware context banks reported by IDR1.
    uint32_t                       Stage2Count;     // Dedicated stage-2 bank count reported by IDR1.
    uint32_t                       SlotCount;       // Total stream match slots reported by IDR0.
    uint32_t                       SIDMask;         // Bitmask of usable Stream ID bits from IDR0.
    uint32_t                       Revision;        // Architecture/minor revision byte from IDR7.
    uint32_t                       InputBits;       // Maximum device address width in bits (32..48).
    uint32_t                       OutputEncoding;  // Encoded physical address width written into VTCR.
    SMMU500Context_t*              Contexts;        // Array of ContextCount bank tracking records.
    SMMU500StreamSlot_t*           Slots;           // Array of SlotCount stream slot tracking records.
    ArmSMMUStage2Unit_t            Stage2;          // Hooks and limits passed to shared stage-2 table code.
    uint64_t                       SyncsSubmitted;  // Monotonic count of TLBGSYNC requests issued.
    uint64_t                       SyncsCompleted;  // Monotonic count of TLBGSYNC requests observed idle.
    SMMU500Sync_t                  StartupSync;     // Saved TLB sync state used at the end of startup.
    SMMU500Sync_t                  ShutdownSync;    // Saved TLB sync state used during hardware shutdown.
};

/**
 * @brief Computes the MMIO byte offset of a context bank from RegisterBase.
 *
 * @param unit  MMU-500 unit whose PageShift and ContextBase were probed.
 * @param index Zero-based context bank index.
 * @return Byte offset of the bank's register page from RegisterBase.
 */
static inline uint32_t
__SMMU500ContextBankOffset(
    _In_ const SMMU500Unit_t* unit,
    _In_ uint32_t             index)
{
    return unit->ContextBase + (index << unit->PageShift);
}

/**
 * @brief Computes the MMIO byte offset of Global Register Space 1 (GR1).
 *
 * Register macros for GR1 (such as CBAR, CBA2R, and CBFRSYNRA) are relative
 * to this page rather than RegisterBase.
 *
 * @param unit MMU-500 unit whose PageShift was probed.
 * @return Byte offset of GR1 from RegisterBase.
 */
static inline uint32_t
__SMMU500GlobalPage1Offset(
    _In_ const SMMU500Unit_t* unit)
{
    return 1U << unit->PageShift;
}

// Hardware programming state of one context bank assigned to a DMA space.
enum SMMU500ContextProgress {
    SMMU500ContextUnused,      // Bank has not been programmed yet.
    SMMU500ContextProgramming, // Registers are currently being written.
    SMMU500ContextEnableSync,  // Bank enabled; waiting on initial VMID TLB sync.
    SMMU500ContextEnabled,     // Bank active and ready for stream routing.
    SMMU500ContextDisableSync, // Bank disabled; waiting on final VMID TLB sync.
    SMMU500ContextDisabled     // Bank turned off and TLB entries flushed.
};

/**
 * @brief Per-address-space state wrapping one MMU-500 context bank.
 *
 * Table holds the root page table and all shared directory pages beneath it,
 * along with the count of active mapping reservations. Shared pages stay in
 * Table until the context bank is disabled and every mapping and stream
 * connection has been destroyed.
 */
struct SMMU500Space {
    element_t                   Link;            // Entry in Common.Spaces list.
    SMMU500Unit_t*              Unit;            // Parent MMU-500 unit.
    uint32_t                    Bank;            // Assigned hardware context bank index.
    ArmSMMUStage2Table_t        Table;           // Stage-2 page table for this space.
    bool                        HasConnection;   // True while a hardware ID connection is attached.
    enum SMMU500ContextProgress ContextProgress; // Current enable/disable state of Bank.
    SMMU500Sync_t               ContextSync;     // Saved TLB sync for enabling or disabling Bank.
};

/**
 * @brief Derives the nonzero VMID tagging TLB entries for a space.
 *
 * VMID 0 is reserved by the driver, so each space uses its bank index plus 1.
 *
 * @param space Space whose VMID is needed.
 * @return Nonzero VMID value in [1, ContextCount].
 */
static inline uint32_t
__SMMU500SpaceVMID(
    _In_ const SMMU500Space_t* space)
{
    return space->Bank + 1U;
}

// Resumable lifecycle state for one DMA mapping in a space.
enum SMMU500MappingPhase {
    SMMU500MappingPrepared,    // Spare table pages reserved; no leaf entries written.
    SMMU500MappingInstalling,  // Leaf entries are being written or synced.
    SMMU500MappingInstalled,   // Leaf entries written and VMID TLB sync finished.
    SMMU500MappingWaitInstall, // Removing: waiting for a pending install sync first.
    SMMU500MappingClearLeaves, // Removing: clearing leaf entries in page tables.
    SMMU500MappingInvalidate,  // Removing: waiting for post-clear VMID TLB sync.
    SMMU500MappingRemoved      // Leaf entries cleared and TLB flushed.
};

/**
 * @brief Per-mapping state for one MMU-500 DMA mapping.
 *
 * Stage2 holds the pre-allocated directory pages and tracks how many bytes of
 * leaf entries have been written. Sync saves the VMID invalidation ticket for
 * both installation and removal retries.
 */
typedef struct SMMU500Mapping {
    ArmSMMUStage2Mapping_t   Stage2;
    enum SMMU500MappingPhase Phase;
    SMMU500Sync_t            Sync;
} SMMU500Mapping_t;

// Resumable lifecycle state for connecting a space's device Stream IDs.
enum SMMU500ConnectionPhase {
    SMMU500ConnectionPrepared,     // Stream slots reserved in software; hardware untouched.
    SMMU500ConnectionContext,      // Enabling the space's context bank.
    SMMU500ConnectionRoutes,       // Writing SMR and S2CR entries up to NextRoute.
    SMMU500ConnectionSync,         // Draining global TLB sync after connecting routes.
    SMMU500ConnectionConnected,    // All Stream IDs are live and translating through Bank.
    SMMU500ConnectionFaultRoutes,  // Disconnecting: faulting slots up to NextRoute.
    SMMU500ConnectionDrain,        // Disconnecting: draining global TLB sync after faulting.
    SMMU500ConnectionDisable,      // Disconnecting: disabling the space's context bank.
    SMMU500ConnectionDisconnected  // All slots faulted, drained, and bank disabled.
};

/**
 * @brief Tracks a set of Stream IDs routed to one MMU-500 space.
 *
 * Routes pairs each device Stream ID (SID) with the hardware stream slot index
 * reserved during Prepare. NextRoute saves progress across timeout retries.
 */
typedef struct SMMU500Connection {
    SMMU500Space_t*             Space;
    uint32_t                    RouteCount;
    uint32_t                    NextRoute;
    enum SMMU500ConnectionPhase Phase;
    SMMU500Sync_t               Sync;
    struct {
        uint32_t SID;
        uint32_t Slot;
    } Routes[];
} SMMU500Connection_t;

/**
 * @brief Waits until any active global TLB sync on the unit finishes.
 *
 * Hold the unit mutex. Must be called before changing registers or page tables,
 * and is also used during startup before any register writes to wait for
 * pre-existing firmware maintenance to finish.
 *
 * @param unit     MMU-500 unit to check.
 * @param deadline Wait deadline initialized by ArmSMMUDeadlineBegin.
 * @return OS_EOK when idle, OS_ETIMEOUT if deadline expires, or OS_ECANCELLED
 *         if a global fault or counter mismatch marks completion unreliable.
 */
KERNELABI oserr_t KERNELAPI
SMMU500SyncWait(
    _In_ SMMU500Unit_t*           unit,
    _In_ const ArmSMMUDeadline_t* deadline);

/**
 * @brief Submits a TLB invalidation and waits for completion, or finishes a saved ticket.
 *
 * Hold the unit mutex. If sync already carries a ticket from an earlier
 * timed-out call, completes that ticket without submitting duplicate work. On
 * timeout, preserves the ticket and all resources so the caller can retry.
 * Unreliable completion pins resources permanently.
 *
 * @param unit     MMU-500 unit to synchronize.
 * @param sync     Sync request prepared by SMMU500SyncPrepare.
 * @param deadline Wait deadline initialized by ArmSMMUDeadlineBegin.
 * @return OS_EOK on completion, OS_EINVALPARAMS for invalid scope or VMID,
 *         OS_ETIMEOUT on timeout, or OS_ECANCELLED if hardware failed.
 */
KERNELABI oserr_t KERNELAPI
SMMU500SyncRun(
    _In_    SMMU500Unit_t*           unit,
    _InOut_ SMMU500Sync_t*           sync,
    _In_    const ArmSMMUDeadline_t* deadline);

/**
 * @brief Initializes a new TLB sync request without submitting it.
 *
 * The request carries Ticket 0 until SMMU500SyncRun submits it to hardware.
 *
 * @param sync  Sync record to initialize.
 * @param scope Whether to invalidate one VMID or all Non-secure entries.
 * @param vmid  Target VMID when scope is SMMU500SyncVMID (ignored otherwise).
 */
KERNELABI void KERNELAPI
SMMU500SyncPrepare(
    _Out_ SMMU500Sync_t*        sync,
    _In_  enum SMMU500SyncScope scope,
    _In_  uint32_t              vmid);

/**
 * @brief Programs and enables a space's assigned context bank.
 *
 * Hold the unit mutex. Streams must still be set to fault and DMA sources must
 * be stopped. Saves progress on timeout so retries resume cleanly.
 *
 * @param space    Space whose context bank should be enabled.
 * @param deadline Wait deadline initialized by ArmSMMUDeadlineBegin.
 * @return OS_EOK when enabled, OS_ETIMEOUT on timeout, OS_EBUSY if the bank is
 *         in an unexpected state, or OS_ECANCELLED on hardware fault/rejection.
 */
KERNELABI oserr_t KERNELAPI
SMMU500ContextEnable(
    _In_ SMMU500Space_t*          space,
    _In_ const ArmSMMUDeadline_t* deadline);

/**
 * @brief Disables a space's context bank and flushes its VMID from the TLB.
 *
 * Hold the unit mutex. Call only after all stream routes to the space have
 * been set to fault and drained. Saves progress on timeout.
 *
 * @param space    Space whose context bank should be disabled.
 * @param deadline Wait deadline initialized by ArmSMMUDeadlineBegin.
 * @return OS_EOK when disabled, OS_ETIMEOUT on timeout, or OS_ECANCELLED if
 *         hardware completion became unreliable.
 */
KERNELABI oserr_t KERNELAPI
SMMU500ContextDisable(
    _In_ SMMU500Space_t*          space,
    _In_ const ArmSMMUDeadline_t* deadline);

/**
 * @brief Samples GFSR for global faults without clearing any status bits.
 *
 * Hold the unit mutex. Used while polling or checking writes so syndrome
 * registers stay intact for SMMU500FaultPoll. Ordinary faults stop new work
 * while keeping checked cleanup possible.
 *
 * @param unit MMU-500 unit to inspect.
 * @return OS_EOK if completion can still be trusted, or OS_ECANCELLED if an
 *         unexpected global fault made hardware completion unreliable.
 */
KERNELABI oserr_t KERNELAPI
SMMU500FaultCheck(
    _In_ SMMU500Unit_t* unit);

/**
 * @brief Captures, logs, and clears global and per-bank fault records.
 *
 * Hold the unit mutex. Clears only the write-1-to-clear bits observed in the
 * snapshot whose syndrome registers were read, then re-checks for racing faults.
 *
 * @param unit MMU-500 unit to poll.
 * @return Generic unit health status from DMAIOMMUUnitCheck.
 */
KERNELABI oserr_t KERNELAPI
SMMU500FaultPoll(
    _In_ SMMU500Unit_t* unit);

/**
 * @brief Writes a 32-bit register and verifies the masked bits read back intact.
 *
 * Hold the unit mutex. Issues a barrier before reading back so posted MMIO
 * writes settle. If any bit selected by mask differs, marks the unit failed
 * and returns OS_ECANCELLED.
 *
 * @param unit   MMU-500 unit to write.
 * @param offset Byte offset from RegisterBase.
 * @param value  32-bit value to write.
 * @param mask   Bitmask of bits that must match on readback.
 * @return OS_EOK on match and clean GFSR check, or OS_ECANCELLED on mismatch
 *         or unreliable global fault.
 */
KERNELABI oserr_t KERNELAPI
SMMU500WriteChecked(
    _In_ SMMU500Unit_t* unit,
    _In_ uint32_t       offset,
    _In_ uint32_t       value,
    _In_ uint32_t       mask);

/**
 * @brief Switches one stream match slot to fault mode and disables matching.
 *
 * Hold the unit mutex. Updates S2CR to fault first and verifies it before
 * clearing SMR, so an active stream can never slip into another slot or route.
 *
 * @param unit MMU-500 unit containing the slot.
 * @param slot Zero-based stream slot index.
 * @return OS_EOK on success, or OS_ECANCELLED if a register write was rejected.
 */
KERNELABI oserr_t KERNELAPI
SMMU500SlotSetFault(
    _In_ SMMU500Unit_t* unit,
    _In_ uint32_t       slot);

/**
 * @brief Connects the shared stage-2 page-table hooks to this MMU-500 unit.
 *
 * Call once after zeroing unit and copying Common.Description. Hardware probing
 * fills in RootLevel, AddressLimit, and PhysicalLimit afterward.
 *
 * @param unit MMU-500 unit to configure.
 */
KERNELABI void KERNELAPI
SMMU500Stage2Initialize(
    _In_ SMMU500Unit_t* unit);

// Generic DMA IOMMU backend callbacks wired into DMAIOMMUBackendOps_t.
// Locking, serialization, and memory lifetime contracts live in dma/backend.h.
// Table callbacks never alter hardware stream routes, and stream callbacks never
// allocate after Prepare or touch mapped data pages.
KERNELABI oserr_t KERNELAPI
SMMU500SpaceCreate(
    _In_    void*  unitContext,
    _InOut_ void** spaceOut);

KERNELABI void KERNELAPI
SMMU500SpaceDestroy(
    _In_ void* unitContext,
    _In_ void* space);

KERNELABI oserr_t KERNELAPI
SMMU500MappingPrepare(
    _In_    void*                          space,
    _In_    const DMAIOMMUMappingConfig_t* config,
    _InOut_ void**                         mappingOut);

KERNELABI oserr_t KERNELAPI
SMMU500HardwareIDsPrepare(
    _In_    void*           space,
    _In_    const uint32_t* hardwareIDs,
    _In_    uint32_t        hardwareIDCount,
    _InOut_ void**          connectionOut);

KERNELABI oserr_t KERNELAPI
SMMU500MappingInstall(
    _In_ void*    space,
    _In_ void*    mapping,
    _In_ uint64_t timeoutNs);

KERNELABI oserr_t KERNELAPI
SMMU500MappingRemove(
    _In_ void*    space,
    _In_ void*    mapping,
    _In_ uint64_t timeoutNs);

KERNELABI void KERNELAPI
SMMU500MappingDestroy(
    _In_ void* space,
    _In_ void* mapping);

KERNELABI oserr_t KERNELAPI
SMMU500HardwareIDsConnect(
    _In_ void*    space,
    _In_ void*    connection,
    _In_ uint64_t timeoutNs);

KERNELABI oserr_t KERNELAPI
SMMU500HardwareIDsDisconnect(
    _In_ void*    space,
    _In_ void*    connection,
    _In_ uint64_t timeoutNs);

KERNELABI void KERNELAPI
SMMU500HardwareIDsDestroy(
    _In_ void* space,
    _In_ void* connection);

#endif //!__ARM_SMMU500_PRIVATE_H__
