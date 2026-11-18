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
 * SMMUv3 Stream ID reservations and stream-table updates.
 * Each DMA-capable device sends a numeric Stream ID (SID) with its memory
 * requests. The SMMU looks up that SID in a two-level stream table to find out
 * whether to block the request or translate it through a space's page table.
 *
 * A connection reserves a full set of SIDs up front and keeps them reserved
 * until the connection is destroyed.
 */

#include <debug.h>
#include <heap.h>
#include <string.h>

#include "private.h"

// SIDBits is capped at 16 during probe, and each L2 table holds 64 SIDs,
// giving at most 1024 L1 slots. A bitmask with one bit per slot fits in
// sixteen 64-bit words.
#define __MAX_STREAM_SLOT_WORDS ((1U << (16U - SMMUV3_STREAM_SPLIT)) / 64U)

// Locate the 64-byte Stream Table Entry (STE) for sid inside its L2 page.
static volatile uint64_t*
__GetStreamEntry(
    _In_ SMMUv3Unit_t* unit,
    _In_ uint32_t      sid)
{
    return (volatile uint64_t*)unit->StreamTables[sid >> SMMUV3_STREAM_SPLIT].Block.VirtualAddress +
           (sid % SMMUV3_STREAM_ENTRIES) * SMMUV3_STREAM_WORDS;
}

static bool
__IsHardwareIDReserved(
    _In_ SMMUv3Unit_t* unit,
    _In_ uint32_t      sid)
{
    SMMUv3StreamTable_t* table = &unit->StreamTables[sid >> SMMUV3_STREAM_SPLIT];

    return (table->ReservedIDs & (1ULL << (sid % SMMUV3_STREAM_ENTRIES))) != 0;
}

static void
__MarkHardwareIDs(
    _In_ SMMUv3Unit_t*   unit,
    _In_ const uint32_t* hardwareIDs,
    _In_ uint32_t        hardwareIDCount,
    _In_ bool            reserved)
{
    for (uint32_t index = 0; index < hardwareIDCount; index++) {
        SMMUv3StreamTable_t* table = &unit->StreamTables[hardwareIDs[index] >> SMMUV3_STREAM_SPLIT];
        uint64_t             bit = 1ULL << (hardwareIDs[index] % SMMUV3_STREAM_ENTRIES);

        if (reserved) {
            table->ReservedIDs |= bit;
        } else {
            table->ReservedIDs &= ~bit;
        }
    }
}

// Check the entire SID array before allocating tables or reserving anything.
// Because hardwareIDCount is capped at 256, checking duplicates pairwise stays
// fast without needing a scratch sort or hash table.
static oserr_t
__ValidateHardwareIDs(
    _In_ SMMUv3Unit_t*   unit,
    _In_ const uint32_t* hardwareIDs,
    _In_ uint32_t        hardwareIDCount)
{
    if (hardwareIDs == NULL || hardwareIDCount == 0 || hardwareIDCount > SMMUV3_MAX_HARDWARE_IDS) {
        ERROR("__ValidateHardwareIDs: missing SID array or invalid count %u at %llx",
              hardwareIDCount, unit->Common.Description.RegisterBase);
        return OS_EINVALPARAMS;
    }

    for (uint32_t index = 0; index < hardwareIDCount; index++) {
        uint32_t sid = hardwareIDs[index];

        if (sid >= (1U << unit->SIDBits)) {
            WARNING("__ValidateHardwareIDs: SID %u exceeds the %u supported bits at %llx",
                    sid, unit->SIDBits, unit->Common.Description.RegisterBase);
            return OS_ENOTSUPPORTED;
        }

        if (__IsHardwareIDReserved(unit, sid)) {
            WARNING("__ValidateHardwareIDs: SID %u is already in use at %llx",
                    sid, unit->Common.Description.RegisterBase);
            return OS_EEXISTS;
        }

        for (uint32_t previous = 0; previous < index; previous++) {
            if (hardwareIDs[previous] == sid) {
                ERROR("__ValidateHardwareIDs: duplicate SID %u at %llx",
                      sid, unit->Common.Description.RegisterBase);
                return OS_EINVALPARAMS;
            }
        }
    }
    return OS_EOK;
}

// Free only the L2 pages allocated during a failed Prepare call. Because they
// were never linked into the L1 root table, hardware could never read them.
static oserr_t
__FreeNewStreamTables(
    _In_ SMMUv3Unit_t*   unit,
    _In_ const uint64_t* allocatedSlots)
{

    for (uint32_t slot = 0; slot < (1U << (unit->SIDBits - SMMUV3_STREAM_SPLIT)); slot++) {
        oserr_t status;
        
        if ((allocatedSlots[slot / 64U] & (1ULL << (slot % 64U))) == 0) {
            continue;
        }

        status = ArmSMMUMemoryFree(
            &unit->Common.Memory,
            &unit->StreamTables[slot].Block
        );
        if (status != OS_EOK) {
            ERROR("__FreeNewStreamTables: freeing L2 table %u at %llx failed: %u",
                  slot, unit->Common.Description.RegisterBase, status);
            return ArmSMMUUnitFail(&unit->Common, status);
        }
    }
    return OS_EOK;
}

// Allocate one 4 KiB L2 stream table page and set all 64 entries to abort
// before linking it into L1, so any unconfigured SID in this block is blocked.
static oserr_t
__AllocateStreamTable(
    _In_ SMMUv3Unit_t* unit,
    _In_ uint32_t      slot)
{
    SMMUv3StreamTable_t* table = &unit->StreamTables[slot];
    uint64_t*            entries;
    oserr_t              status;

    status = ArmSMMUMemoryAllocate(
        &unit->Common.Memory,
        ARM_SMMU_MEMORY_PAGE_SIZE,
        ARM_SMMU_MEMORY_PAGE_SIZE,
        &table->Block
    );
    if (status != OS_EOK) {
        ERROR("__AllocateStreamTable: allocating L2 table %u at %llx failed: %u",
              slot, unit->Common.Description.RegisterBase, status);
        if (unit->Common.Memory.State == ArmSMMUMemoryFailed) {
            ArmSMMUUnitFail(&unit->Common, status);
        }
        return status;
    }

    entries = table->Block.VirtualAddress;
    for (uint32_t entry = 0; entry < SMMUV3_STREAM_ENTRIES; entry++) {
        entries[entry * SMMUV3_STREAM_WORDS] = SMMUV3_STE_ABORT;
    }
    return OS_EOK;
}

// Allocate L2 pages for any SIDs whose L1 slot has no page yet. Tracking only
// newly allocated slots lets a failed Prepare roll back cleanly without
// touching L2 pages already shared with other connections.
static oserr_t
__AllocateStreamTables(
    _In_    SMMUv3Unit_t*   unit,
    _In_    const uint32_t* hardwareIDs,
    _In_    uint32_t        hardwareIDCount,
    _InOut_ uint64_t*       allocatedSlots)
{

    for (uint32_t index = 0; index < hardwareIDCount; index++) {
        uint32_t slot = hardwareIDs[index] >> SMMUV3_STREAM_SPLIT;
        oserr_t  status;
        
        if (unit->StreamTables[slot].Block.Length != 0) {
            continue;
        }

        status = __AllocateStreamTable(unit, slot);
        if (status != OS_EOK) {
            return status;
        }
        allocatedSlots[slot / 64U] |= 1ULL << (slot % 64U);
    }
    return OS_EOK;
}

oserr_t
SMMUv3HardwareIDsPrepare(
    _In_    void*           spaceContext,
    _In_    const uint32_t* hardwareIDs,
    _In_    uint32_t        hardwareIDCount,
    _InOut_ void**          connectionOut)
{
    SMMUv3Space_t*      space = spaceContext;
    SMMUv3Unit_t*       unit = space->Unit;
    SMMUv3Connection_t* connection;
    uint64_t            allocatedSlots[__MAX_STREAM_SLOT_WORDS] = { 0 };
    oserr_t             status;
    oserr_t             cleanupStatus;

    if (space->HasConnection) {
        WARNING("SMMUv3HardwareIDsPrepare: VMID %u at %llx already has a connection",
                space->VMID, unit->Common.Description.RegisterBase);
        return OS_EBUSY;
    }

    status = __ValidateHardwareIDs(unit, hardwareIDs, hardwareIDCount);
    if (status != OS_EOK) {
        return status;
    }

    connection = kmalloc(sizeof(SMMUv3Connection_t) + hardwareIDCount * sizeof(uint32_t));
    if (connection == NULL) {
        ERROR("SMMUv3HardwareIDsPrepare: allocating connection for VMID %u at %llx failed",
              space->VMID, unit->Common.Description.RegisterBase);
        return OS_EOOM;
    }

    memset(connection, 0, sizeof(SMMUv3Connection_t));
    connection->Space = space;
    connection->Count = hardwareIDCount;
    memcpy(connection->HardwareIDs, hardwareIDs, hardwareIDCount * sizeof(uint32_t));

    // Allocate missing L2 pages now so Connect never runs out of memory while
    // updating live hardware stream entries.
    status = __AllocateStreamTables(unit, hardwareIDs, hardwareIDCount, allocatedSlots);
    if (status != OS_EOK) {
        cleanupStatus = __FreeNewStreamTables(unit, allocatedSlots);
        kfree(connection);
        if (cleanupStatus != OS_EOK) {
            return cleanupStatus;
        }
        return status;
    }
    
    __MarkHardwareIDs(unit, hardwareIDs, hardwareIDCount, true);

    space->HasConnection = true;
    *connectionOut = connection;
    TRACE("SMMUv3HardwareIDsPrepare: reserved %u SIDs for VMID %u at %llx",
          hardwareIDCount, space->VMID, unit->Common.Description.RegisterBase);
    return OS_EOK;
}

static void
__PrepareEntryInvalidation(
    _In_ SMMUv3CommandBatch_t* batch,
    _In_ uint32_t              sid,
    _In_ bool                  leaf)
{
    memset(batch, 0, sizeof(*batch));
    batch->Count = 2;
    batch->Commands[0].Words[0] = SMMUV3_CMD_CFGI_STE | ((uint64_t)sid << SMMUV3_CMD_ID_SHIFT);
    batch->Commands[0].Words[1] = leaf ? SMMUV3_CMD_CFGI_LEAF : 0;
    batch->Commands[1].Words[0] = SMMUV3_CMD_SYNC;
}

// Drop cached stream configuration for sid and wait for completion.
// When leaf is true, only the 64-byte L2 entry for sid is flushed. When leaf
// is false, hardware also flushes any cached Level-1 pointer to its L2 page.
static oserr_t
__InvalidateEntry(
    _In_ SMMUv3Unit_t*           unit,
    _In_ SMMUv3CommandBatch_t*   batch,
    _In_ uint32_t                sid,
    _In_ bool                    leaf,
    _In_ const ArmSMMUDeadline_t* deadline)
{
    __PrepareEntryInvalidation(batch, sid, leaf);
    return SMMUv3CommandBatchRun(unit, batch, deadline);
}

static void
__WriteTranslationFields(
    _In_ SMMUv3Space_t*     space,
    _In_ volatile uint64_t* entry)
{
    SMMUv3Unit_t* unit = space->Unit;
    uint32_t      control;

    // The STE was already cleared (V=0) and invalidated, so hardware will not
    // read it until V is turned back on later. Keep V clear while writing the
    // translation settings: S2S=0 disables transaction stalling, S2R=1 logs
    // faults to the event ring, and ATS/hardware dirty-access flags stay off.
    control = (64U - unit->AddressBits) |
              ((2U - unit->Stage2.RootLevel) << SMMUV3_VTCR_SL0_SHIFT) |
              SMMUV3_VTCR_OUTER_SHARED | (unit->AddressEncoding << SMMUV3_VTCR_PS_SHIFT);
    if (unit->Common.Memory.Allocation.Cached) {
        control |= SMMUV3_VTCR_CACHE_WB;
    }

    for (uint32_t index = 1; index < SMMUV3_STREAM_WORDS; index++) {
        entry[index] = 0;
    }

    entry[2] = space->VMID | ((uint64_t)control << SMMUV3_STE_VTCR_SHIFT) |
               SMMUV3_STE_AARCH64 | SMMUV3_STE_RECORD_FAULT;
    entry[3] = space->Table.Root.PhysicalAddress;
    entry[0] = SMMUV3_STE_STAGE2 & ~1ULL;
}

// Publish the L2 page into its L1 root slot on first use. Barriers make sure
// the pre-filled abort entries inside the L2 page are visible in RAM before
// the SMMU can follow the L1 pointer, and flush the L1 entry out afterward.
static void
__LinkStreamTable(
    _In_ SMMUv3Unit_t* unit,
    _In_ uint32_t      slot)
{
    SMMUv3StreamTable_t* table = &unit->StreamTables[slot];
    volatile uint64_t*   root = unit->StreamRoot.VirtualAddress;

    if (table->Published) {
        return;
    }

    __SMMUv3Barrier();
    root[slot] = table->Block.PhysicalAddress | SMMUV3_L1_SPAN;
    table->Published = true;
    __SMMUv3Barrier();
}

// Wire up one SID following ARM's required multi-step STE update sequence.
// Because a 64-byte STE spans eight 64-bit words, writing all fields and V=1
// together would let hardware observe a half-written entry. Instead, each step
// writes part of the state and waits for a stream-table invalidation:
// 1. Link the L2 page into L1 and flush L1+L2 caches.
// 2. Clear V=0 (invalid) and flush so hardware drops any old ABORT entry.
// 3. Populate translation fields with V still 0 and flush again.
// 4. Set V=1 to enable stage-2 translation and flush so hardware reloads it.
static oserr_t
__ConnectStream(
    _In_ SMMUv3Space_t*           space,
    _In_ SMMUv3Connection_t*      connection,
    _In_ const ArmSMMUDeadline_t* deadline)
{
    SMMUv3Unit_t*      unit = space->Unit;
    uint32_t           sid = connection->HardwareIDs[connection->Index];
    volatile uint64_t* entry = __GetStreamEntry(unit, sid);
    oserr_t            status;

    if (connection->Phase == SMMUv3ConnectionLinkTable) {
        __LinkStreamTable(unit, sid >> SMMUV3_STREAM_SPLIT);

        // Always run a non-leaf invalidation here, even if Published was
        // already true, in case an earlier Connect linked this slot and timed
        // out before its SYNC finished.
        status = __InvalidateEntry(unit, &connection->Batch, sid, false, deadline);
        if (status != OS_EOK) {
            return status;
        }
        connection->Phase = SMMUv3ConnectionDisableEntry;
    }

    if (connection->Phase == SMMUv3ConnectionDisableEntry) {
        entry[0] = 0;
        __SMMUv3Barrier();

        status = __InvalidateEntry(unit, &connection->Batch, sid, true, deadline);
        if (status != OS_EOK) {
            return status;
        }
        connection->Phase = SMMUv3ConnectionWriteEntry;
    }

    if (connection->Phase == SMMUv3ConnectionWriteEntry) {
        __WriteTranslationFields(space, entry);
        __SMMUv3Barrier();
        
        status = __InvalidateEntry(unit, &connection->Batch, sid, true, deadline);
        if (status != OS_EOK) {
            return status;
        }
        connection->Phase = SMMUv3ConnectionEnableEntry;
    }

    if (connection->Phase == SMMUv3ConnectionEnableEntry) {
        // Turning V from 0 to 1 requires a final invalidation before Connect
        // can report success, so any cached invalid state is cleared.
        entry[0] = SMMUV3_STE_STAGE2;
        __SMMUv3Barrier();

        status = __InvalidateEntry(unit, &connection->Batch, sid, true, deadline);
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}

oserr_t
SMMUv3HardwareIDsConnect(
    _In_ void*    spaceContext,
    _In_ void*    connectionContext,
    _In_ uint64_t timeoutNs)
{
    SMMUv3Space_t*      space = spaceContext;
    SMMUv3Connection_t* connection = connectionContext;
    ArmSMMUDeadline_t   deadline;
    oserr_t             status;

    if (connection->Phase != SMMUv3ConnectionPrepared) {
        WARNING("SMMUv3HardwareIDsConnect: VMID %u at %llx cannot connect from phase %u",
                space->VMID, space->Unit->Common.Description.RegisterBase, (uint32_t)connection->Phase);
        return OS_EBUSY;
    }

    connection->Phase = SMMUv3ConnectionLinkTable;
    status = ArmSMMUDeadlineBegin(timeoutNs, &deadline);
    if (status != OS_EOK) {
        return status;
    }

    // Step through each Stream ID in order. If a step times out, Index and
    // Phase keep the exact spot where setup paused so Disconnect can clean up.
    while (connection->Index < connection->Count) {
        if (ArmSMMUDeadlineExpired(&deadline)) {
            WARNING("SMMUv3HardwareIDsConnect: timeout at %llx before SID %u, %u of %u connected",
                    space->Unit->Common.Description.RegisterBase, connection->HardwareIDs[connection->Index],
                    connection->Index, connection->Count);
            return OS_ETIMEOUT;
        }

        status = __ConnectStream(space, connection, &deadline);
        if (status != OS_EOK) {
            WARNING("SMMUv3HardwareIDsConnect: SID %u at %llx stopped in phase %u: %u",
                    connection->HardwareIDs[connection->Index], space->Unit->Common.Description.RegisterBase,
                    (uint32_t)connection->Phase, status);
            return status;
        }
        connection->Index++;
        connection->Phase = SMMUv3ConnectionLinkTable;
    }

    connection->Phase = SMMUv3ConnectionConnected;
    TRACE("SMMUv3HardwareIDsConnect: %u SIDs use VMID %u at %llx",
          connection->Count, space->VMID, space->Unit->Common.Description.RegisterBase);
    return OS_EOK;
}

// Switch every SID's STE back to abort, starting at connection->Index.
// When Batch.Count is zero, the current SID has not been updated yet, so we
// write the abort entry and prepare its invalidation. On retry after a timeout,
// a populated Batch keeps its submitted command state and simply finishes waiting.
static oserr_t
__AbortStreams(
    _In_ SMMUv3Unit_t*            unit,
    _In_ SMMUv3Connection_t*      connection,
    _In_ const ArmSMMUDeadline_t* deadline)
{
    volatile uint64_t* entry;
    uint32_t           sid;
    oserr_t            status;

    while (connection->Index < connection->Count) {
        if (ArmSMMUDeadlineExpired(deadline)) {
            WARNING("__AbortStreams: timeout at %llx before SID %u, %u of %u aborted",
                    unit->Common.Description.RegisterBase, connection->HardwareIDs[connection->Index],
                    connection->Index, connection->Count);
            return OS_ETIMEOUT;
        }

        sid = connection->HardwareIDs[connection->Index];
        if (connection->Batch.Count == 0) {
            entry = __GetStreamEntry(unit, sid);
            // Writing V=1 and Config=abort is a single 64-bit store to word 0.
            // Hardware ignores the remaining STE words once Config is abort, so
            // they do not need to be cleared.
            entry[0] = SMMUV3_STE_ABORT;
            __SMMUv3Barrier();
            __PrepareEntryInvalidation(&connection->Batch, sid, true);
        }

        status = SMMUv3CommandBatchRun(unit, &connection->Batch, deadline);
        if (status != OS_EOK) {
            WARNING("__AbortStreams: abort invalidation for SID %u at %llx failed: %u",
                    sid, unit->Common.Description.RegisterBase, status);
            return status;
        }

        connection->Index++;
        memset(&connection->Batch, 0, sizeof(connection->Batch));
    }
    return OS_EOK;
}

// Wait for any command batch left in flight by a timed-out Connect call before
// overwriting its STE. Unsubmitted batches never reached hardware and can be
// discarded immediately.
static oserr_t
__FinishPendingConnection(
    _In_ SMMUv3Unit_t*            unit,
    _In_ SMMUv3Connection_t*      connection,
    _In_ const ArmSMMUDeadline_t* deadline)
{
    oserr_t status;

    if (connection->Batch.State == SMMUv3BatchSubmitted) {
        status = SMMUv3CommandBatchRun(unit, &connection->Batch, deadline);
        if (status != OS_EOK) {
            return status;
        }
    }

    memset(&connection->Batch, 0, sizeof(connection->Batch));
    connection->Index = 0;
    connection->Phase = SMMUv3ConnectionAbortEntries;
    return OS_EOK;
}

oserr_t
SMMUv3HardwareIDsDisconnect(
    _In_ void*    spaceContext,
    _In_ void*    connectionContext,
    _In_ uint64_t timeoutNs)
{
    SMMUv3Space_t*      space = spaceContext;
    SMMUv3Unit_t*       unit = space->Unit;
    SMMUv3Connection_t* connection = connectionContext;
    ArmSMMUDeadline_t   deadline;
    oserr_t             status;

    if (connection->Phase == SMMUv3ConnectionDisconnected) {
        return OS_EOK;
    }

    if (connection->Phase == SMMUv3ConnectionPrepared) {
        connection->Phase = SMMUv3ConnectionDisconnected;
        return OS_EOK;
    }

    status = ArmSMMUDeadlineBegin(timeoutNs, &deadline);
    if (status != OS_EOK) {
        return status;
    }

    // Disconnect runs three resumable steps in order:
    // 1. Wait for any in-flight command batch left over from Connect.
    // 2. Switch every SID's STE back to abort and invalidate each entry.
    // 3. Flush the space's VMID from the TLB so old translations are gone.
    if (connection->Phase < SMMUv3ConnectionWaitPending) {
        connection->Phase = SMMUv3ConnectionWaitPending;
    }

    if (connection->Phase == SMMUv3ConnectionWaitPending) {
        status = __FinishPendingConnection(unit, connection, &deadline);
        if (status != OS_EOK) {
            return status;
        }
    }

    if (connection->Phase == SMMUv3ConnectionAbortEntries) {
        status = __AbortStreams(unit, connection, &deadline);
        if (status != OS_EOK) {
            return status;
        }
        SMMUv3CommandBatchPrepareTLBInvalidation(&connection->Batch, space->VMID);
        connection->Phase = SMMUv3ConnectionFlushTLB;
    }

    status = SMMUv3CommandBatchRun(unit, &connection->Batch, &deadline);
    if (status != OS_EOK) {
        return status;
    }

    connection->Phase = SMMUv3ConnectionDisconnected;
    TRACE("SMMUv3HardwareIDsDisconnect: %u SIDs left VMID %u at %llx",
          connection->Count, space->VMID, unit->Common.Description.RegisterBase);
    return OS_EOK;
}

void
SMMUv3HardwareIDsDestroy(
    _In_ void* spaceContext,
    _In_ void* connectionContext)
{
    SMMUv3Space_t*      space = spaceContext;
    SMMUv3Unit_t*       unit = space->Unit;
    SMMUv3Connection_t* connection = connectionContext;

    // Only connections that never connected or finished disconnecting may free
    // their SID reservations.
    if ((connection->Phase != SMMUv3ConnectionPrepared && connection->Phase != SMMUv3ConnectionDisconnected) ||
        connection->Space != space || !space->HasConnection) {
        ERROR("SMMUv3HardwareIDsDestroy: connection for VMID %u at %llx is active or belongs to another space, phase %u",
              space->VMID, unit->Common.Description.RegisterBase, (uint32_t)connection->Phase);
        ArmSMMUUnitFail(&unit->Common, OS_EBUSY);
        return;
    }

    __MarkHardwareIDs(unit, connection->HardwareIDs, connection->Count, false);
    space->HasConnection = false;
    kfree(connection);
}
