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
 * SMMUv3 internal state and module interfaces.
 *
 * Locking rule:
 * Except for Common.State (which is atomic), every mutable field in these
 * structs is protected by Common.Backend's unit mutex. Generic DMA callbacks
 * enter with that mutex held, and the native unit functions in unit.c acquire
 * it before calling into these modules.
 */

#ifndef __ARM_SMMUV3_PRIVATE_H__
#define __ARM_SMMUV3_PRIVATE_H__

#include <ds/list.h>
#include <stdatomic.h>

#include "smmuv3.h"
#include "../deadline.h"
#include "../memory.h"
#include "../stage2.h"
#include "../stage2-table.h"
#include "../unit-common.h"
#include "registers.h"

// Upper bound on Stream IDs attached in one connection call, keeping stack
// scratch buffers small and duplicate checks fast.
#define SMMUV3_MAX_HARDWARE_IDS 256U

// Tracks how far SMMUv3UnitStart has progressed through register writes.
// Each "Issued" state means software has written the register, though hardware
// may not have acknowledged it yet. SMMUv3UnitDestroy reads this value so it
// finishes waiting for any pending acknowledgement before changing the register
// again, while leaving untouched any earlier registers that firmware may own.
enum SMMUv3StartProgress {
    SMMUv3NoWrites,
    SMMUv3AbortIssued,
    SMMUv3IrqDisableIssued,
    SMMUv3DisableIssued,
    SMMUv3BasesWritten,
    SMMUv3CommandEnableIssued,
    SMMUv3StartupInvalidation,
    SMMUv3EventEnableIssued,
    SMMUv3TranslationEnableIssued,
    SMMUv3StartupComplete
};

// Tracks teardown steps in order so a timed-out SMMUv3UnitDestroy can resume
// from where it stopped. Once HardwareStopped is reached, the controller is
// idle and no longer reads control memory, so the pool can be freed safely.
enum SMMUv3CloseProgress {
    SMMUv3CloseInitial,
    SMMUv3CloseTranslationIssued,
    SMMUv3CloseInvalidation,
    SMMUv3CloseQueuesIssued,
    SMMUv3CloseHardwareStopped
};

// One 16-byte entry in the hardware command ring.
typedef struct SMMUv3Command {
    uint64_t Words[2];
} SMMUv3Command_t;

enum SMMUv3BatchState {
    SMMUv3BatchPrepared,
    SMMUv3BatchSubmitted,
    SMMUv3BatchComplete
};

/**
 * @brief Tracks one command sequence from preparation through completion.
 *
 * A batch holds 1 to 3 commands ending with CMD_SYNC. Once submitted, its
 * contents stay unchanged across timeout retries so it is never queued twice.
 * EndPosition records the total number of commands submitted up to the end of
 * this batch, so completion stays unambiguous even if other callers advance or
 * wrap the ring buffer.
 */
typedef struct SMMUv3CommandBatch {
    SMMUv3Command_t       Commands[3];
    uint32_t              Count;
    enum SMMUv3BatchState State;
    uint64_t              EndPosition;
} SMMUv3CommandBatch_t;

/**
 * @brief Software record for one Level-1 stream table slot (covering 64 SIDs).
 *
 * Block points to the 4 KiB Level-2 page holding 64 Stream Table Entries.
 * ReservedIDs is a 64-bit mask of SIDs currently reserved by connections in
 * this slot. Once Published is true, the Level-1 root points to Block, and the
 * page stays allocated until the whole unit is destroyed.
 */
typedef struct SMMUv3StreamTable {
    ArmSMMUMemoryBlock_t Block;
    uint64_t             ReservedIDs;
    bool                 Published;
} SMMUv3StreamTable_t;

/**
 * @brief Complete software and hardware state for one SMMUv3 controller.
 *
 * Control mirrors the latest value written to CR0. CommandsSubmitted and
 * CommandsCompleted count total commands from zero across all ring wraps
 * without resetting. EventConsumer holds the software copy of EVTQ_CONS, keeping both the ring
 * wrap bit and the overflow acknowledgement bit. StartupBatch and ShutdownBatch
 * hold the full-cache invalidation batches used during start and teardown.
 */
struct SMMUv3Unit {
    ArmSMMUUnitCommon_t      Common;
    enum SMMUv3StartProgress StartProgress;
    enum SMMUv3CloseProgress CloseProgress;
    bool                     ATSSupported;
    uint32_t                 Control;
    uint32_t                 AddressBits;
    uint32_t                 AddressEncoding;
    uint32_t                 SIDBits;
    uint32_t                 VMIDCount;
    uint32_t                 CommandBits;
    uint32_t                 EventBits;
    uint64_t*                VMIDs;
    SMMUv3StreamTable_t*     StreamTables;
    ArmSMMUStage2Unit_t      Stage2;
    ArmSMMUMemoryBlock_t     StreamRoot;
    ArmSMMUMemoryBlock_t     Commands;
    ArmSMMUMemoryBlock_t     Events;
    uint64_t                 CommandsSubmitted;
    uint64_t                 CommandsCompleted;
    uint32_t                 EventConsumer;
    SMMUv3CommandBatch_t     StartupBatch;
    SMMUv3CommandBatch_t     ShutdownBatch;
};

/**
 * @brief One isolated stage-2 DMA address space on an SMMUv3 unit.
 *
 * Each space owns a unique non-zero hardware VMID that tags its TLB entries.
 * Table holds the root page table, all lower-level directory pages, and the
 * count of prepared mappings. HasConnection is true while a set of device
 * Stream IDs is prepared or connected for this space.
 */
typedef struct SMMUv3Space {
    element_t            Link;
    SMMUv3Unit_t*        Unit;
    uint32_t             VMID;
    ArmSMMUStage2Table_t Table;
    bool                 HasConnection;
} SMMUv3Space_t;

// Tracks install and removal progress for one DMA mapping so timed-out calls
// can resume safely without redoing completed steps.
enum SMMUv3MappingPhase {
    SMMUv3MappingPrepared,
    SMMUv3MappingInstalling,
    SMMUv3MappingInstalled,
    SMMUv3MappingWaitInstall,
    SMMUv3MappingClearLeaves,
    SMMUv3MappingInvalidate,
    SMMUv3MappingRemoved
};

/**
 * @brief Per-mapping state for installing and removing a stage-2 DMA range.
 *
 * Stage2 holds the pre-allocated directory pages and how many bytes currently
 * have leaf entries written. Batch tracks the TLB invalidation that follows
 * leaf writes or clears.
 */
typedef struct SMMUv3Mapping {
    ArmSMMUStage2Mapping_t  Stage2;
    enum SMMUv3MappingPhase Phase;
    SMMUv3CommandBatch_t    Batch;
} SMMUv3Mapping_t;

// Tracks binding and unbinding a group of device Stream IDs to a space.
// Connect walks LinkTable through EnableEntry for each SID, waiting for a
// stream-table invalidation after every step. Disconnect waits for any pending
// Connect batch, switches each SID back to abort, and flushes the VMID's TLB.
enum SMMUv3ConnectionPhase {
    SMMUv3ConnectionPrepared,
    SMMUv3ConnectionLinkTable,
    SMMUv3ConnectionDisableEntry,
    SMMUv3ConnectionWriteEntry,
    SMMUv3ConnectionEnableEntry,
    SMMUv3ConnectionConnected,
    SMMUv3ConnectionWaitPending,
    SMMUv3ConnectionAbortEntries,
    SMMUv3ConnectionFlushTLB,
    SMMUv3ConnectionDisconnected
};

/**
 * @brief Reservation and connection state for a set of device Stream IDs.
 *
 * All HardwareIDs remain reserved from Prepare until Destroy, including across
 * partial or timed-out Connect and Disconnect calls. Index identifies the SID
 * currently being updated, and Batch saves that step's pending invalidation.
 */
typedef struct SMMUv3Connection {
    SMMUv3Space_t*             Space;
    uint32_t                   Count;
    uint32_t                   Index;
    enum SMMUv3ConnectionPhase Phase;
    SMMUv3CommandBatch_t       Batch;
    uint32_t                   HardwareIDs[];
} SMMUv3Connection_t;

/**
 * @brief Submits a command batch once and waits until its final CMD_SYNC finishes.
 *
 * Hold the unit mutex. Allocates no memory. If the deadline expires first,
 * any already-submitted batch stays intact so a retry only waits for its saved
 * EndPosition instead of queuing commands a second time. An invalid consumer
 * index or hardware error marks the unit failed and returns OS_ECANCELLED.
 *
 * @param unit     SMMUv3 unit owning the command ring.
 * @param batch    Prepared or previously submitted command batch.
 * @param deadline Timeout deadline for completion.
 * @return OS_EOK on completion, OS_EINVALPARAMS for an invalid batch,
 *         OS_ETIMEOUT if the deadline expired, or OS_ECANCELLED on hardware fault.
 */
KERNELABI oserr_t KERNELAPI
SMMUv3CommandBatchRun(
    _In_    SMMUv3Unit_t*           unit,
    _InOut_ SMMUv3CommandBatch_t*   batch,
    _In_    const ArmSMMUDeadline_t* deadline);

/**
 * @brief Fills batch with a VMID TLB invalidation followed by CMD_SYNC.
 *
 * Discards any earlier contents of batch.
 *
 * @param batch Batch structure to initialize.
 * @param vmid  Non-zero VMID whose cached translations should be flushed.
 */
KERNELABI void KERNELAPI
SMMUv3CommandBatchPrepareTLBInvalidation(
    _Out_ SMMUv3CommandBatch_t* batch,
    _In_  uint32_t              vmid);

/**
 * @brief Checks the global error registers for hardware failures.
 *
 * Hold the unit mutex. Any unacknowledged bit in GERROR marks the unit failed
 * permanently, stopping further command submissions because completion can no
 * longer be trusted.
 *
 * @param unit SMMUv3 unit to inspect.
 * @return OS_EOK if no global errors are active, or OS_ECANCELLED on failure.
 */
KERNELABI oserr_t KERNELAPI
SMMUv3FaultCheck(
    _In_ SMMUv3Unit_t* unit);

/**
 * @brief Checks global errors and turns queued event records into fault records.
 *
 * Hold the unit mutex. The event queue must have been enabled by Start. Each
 * event record read from the queue is either a device fault, which is written
 * to faults, or a unit failure, which is logged and fails the unit through
 * ArmSMMUUnitFail. See SMMUv3UnitPoll for the full rules.
 *
 * A queue overflow is acknowledged and reported as one DMAIOMMUFaultReportsLost
 * record in faults[0]. It leaves capacity - 1 slots for queued events. Queue
 * pointers that report more records than the queue holds fail the unit, and
 * nothing is read.
 *
 * @param unit     SMMUv3 unit to drain.
 * @param faults   Receives up to capacity fault records.
 * @param capacity Number of records faults can hold. Must be at least 1.
 * @param countOut Receives the number of records written. Always set.
 * @return OS_EOK if the unit is healthy, or OS_ECANCELLED if it has failed.
 *         Records written before a failure stay valid.
 */
KERNELABI oserr_t KERNELAPI
SMMUv3FaultDrain(
    _In_  SMMUv3Unit_t*    unit,
    _Out_ DMAIOMMUFault_t* faults,
    _In_  uint32_t         capacity,
    _Out_ uint32_t*        countOut);

/**
 * @brief Connects the shared stage-2 page-table hooks to this SMMUv3 unit.
 *
 * Call once after unit is zeroed and its firmware description is copied.
 * Probing fills in AddressLimit, PhysicalLimit, and RootLevel later.
 *
 * @param unit SMMUv3 unit whose Stage2 interface should be wired up.
 */
KERNELABI void KERNELAPI
SMMUv3Stage2Initialize(
    _In_ SMMUv3Unit_t* unit);

// Generic DMA IOMMU backend callbacks (contracts are defined in dma/backend.h).
// All callbacks enter with the unit mutex held. Table callbacks update page
// tables and VMID TLBs without changing device Stream ID routes. Stream
// callbacks never allocate memory after Prepare and never free mapped data RAM.
KERNELABI oserr_t KERNELAPI
SMMUv3SpaceCreate(
    _In_     void*  unitContext,
    _InOut_  void** spaceOut);

KERNELABI void KERNELAPI
SMMUv3SpaceDestroy(
    _In_     void* unitContext,
    _In_     void* space);

KERNELABI oserr_t KERNELAPI
SMMUv3MappingPrepare(
    _In_     void*                          space,
    _In_     const DMAIOMMUMappingConfig_t* config,
    _InOut_  void**                         mappingOut);

KERNELABI oserr_t KERNELAPI
SMMUv3HardwareIDsPrepare(
    _In_     void*           space,
    _In_     const uint32_t* hardwareIDs,
    _In_     uint32_t        hardwareIDCount,
    _InOut_  void**          connectionOut);

KERNELABI oserr_t KERNELAPI
SMMUv3MappingInstall(
    _In_     void*    space,
    _In_     void*    mapping,
    _In_     uint64_t timeoutNs);

KERNELABI oserr_t KERNELAPI
SMMUv3MappingRemove(
    _In_     void*    space,
    _In_     void*    mapping,
    _In_     uint64_t timeoutNs);

KERNELABI void KERNELAPI
SMMUv3MappingDestroy(
    _In_     void* space,
    _In_     void* mapping);

KERNELABI oserr_t KERNELAPI
SMMUv3HardwareIDsConnect(
    _In_     void*    space,
    _In_     void*    connection,
    _In_     uint64_t timeoutNs);

KERNELABI oserr_t KERNELAPI
SMMUv3HardwareIDsDisconnect(
    _In_     void*    space,
    _In_     void*    connection,
    _In_     uint64_t timeoutNs);

KERNELABI void KERNELAPI
SMMUv3HardwareIDsDestroy(
    _In_     void* space,
    _In_     void* connection);

#endif //!__ARM_SMMUV3_PRIVATE_H__
