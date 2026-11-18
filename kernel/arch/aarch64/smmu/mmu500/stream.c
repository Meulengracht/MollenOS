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
 * MMU-500 stream ID routing and slot management.
 * Devices behind an MMU-500 tag DMA requests with a Stream ID (SID). Each
 * hardware stream slot pairs a match register (SMR) with a routing register
 * (S2CR). Preparation reserves the full SID set in software without touching
 * hardware. Connect and Disconnect allocate no memory and save each completed
 * step so timeouts can resume safely.
 */

#include "private.h"
#include <debug.h>
#include <heap.h>
#include <limits.h>
#include <string.h>

oserr_t
SMMU500SlotSetFault(
    _In_ SMMU500Unit_t* unit,
    _In_ uint32_t       slot)
{
    oserr_t status;

    // Switch S2CR to fault mode and verify it before clearing SMR. Clearing
    // SMR first could let an active stream match a later slot or hit a stale
    // policy before routing closes.
    status = SMMU500WriteChecked(
        unit,
        SMMU500_S2CR(slot),
        SMMU500_S2CR_FAULT,
        SMMU500_S2CR_FAULT_FIELDS
    );
    if (status != OS_EOK) {
        return status;
    }
    return SMMU500WriteChecked(
        unit,
        SMMU500_SMR(slot),
        0,
        SMMU500_SMR_FIELDS
    );
}

// Validate every requested Stream ID before reserving any slot: reject bits
// outside the hardware SIDMask, duplicates in the input list, and SIDs already
// reserved by another address space on this unit.
static oserr_t
__ValidateHardwareIDs(
    _In_ SMMU500Unit_t*  unit,
    _In_ const uint32_t* hardwareIDs,
    _In_ uint32_t        hardwareIDCount)
{
    uint32_t index;
    uint32_t previous;
    uint32_t slot;

    for (index = 0; index < hardwareIDCount; index++) {
        if ((hardwareIDs[index] & ~unit->SIDMask) != 0) {
            WARNING("__ValidateHardwareIDs: SID %x exceeds the SID mask %x at %llx",
                    hardwareIDs[index], unit->SIDMask, unit->Common.Description.RegisterBase);
            return OS_ENOTSUPPORTED;
        }

        for (previous = 0; previous < index; previous++) {
            if (hardwareIDs[previous] == hardwareIDs[index]) {
                WARNING("__ValidateHardwareIDs: SID %x is listed twice", hardwareIDs[index]);
                return OS_EINVALPARAMS;
            }
        }

        for (slot = 0; slot < unit->SlotCount; slot++) {
            if (unit->Slots[slot].Reserved && unit->Slots[slot].SID == hardwareIDs[index]) {
                WARNING("__ValidateHardwareIDs: SID %x is already reserved at %llx, slot %u",
                        hardwareIDs[index], unit->Common.Description.RegisterBase, slot);
                return OS_EEXISTS;
            }
        }
    }
    return OS_EOK;
}

// Count unreserved hardware stream slots on this controller.
static uint32_t
__CountFreeSlots(
    _In_ SMMU500Unit_t* unit)
{
    uint32_t freeSlots = 0;
    uint32_t slot;

    for (slot = 0; slot < unit->SlotCount; slot++) {
        if (!unit->Slots[slot].Reserved) {
            freeSlots++;
        }
    }
    return freeSlots;
}

// Assign each SID the lowest free stream slot. The caller already checked that
// enough free slots exist, so this always succeeds.
static void
__ReserveSlots(
    _In_ SMMU500Unit_t*       unit,
    _In_ SMMU500Connection_t* connection,
    _In_ const uint32_t*      hardwareIDs)
{
    uint32_t slot = 0;

    for (uint32_t index = 0; index < connection->RouteCount; index++) {
        while (unit->Slots[slot].Reserved) {
            slot++;
        }

        unit->Slots[slot].Reserved = true;
        unit->Slots[slot].SID = hardwareIDs[index];

        connection->Routes[index].SID = hardwareIDs[index];
        connection->Routes[index].Slot = slot++;
    }
}

oserr_t
SMMU500HardwareIDsPrepare(
    _In_    void*           spaceContext,
    _In_    const uint32_t* hardwareIDs,
    _In_    uint32_t        hardwareIDCount,
    _InOut_ void**          connectionOut)
{
    SMMU500Space_t*      space = spaceContext;
    SMMU500Unit_t*       unit = space->Unit;
    SMMU500Connection_t* connection;
    size_t               size;
    oserr_t              status;

    if (hardwareIDs == NULL || hardwareIDCount == 0 || connectionOut == NULL) {
        WARNING("SMMU500HardwareIDsPrepare: missing stream IDs or output for bank %u at %llx",
                space->Bank, unit->Common.Description.RegisterBase);
        return OS_EINVALPARAMS;
    }

    if (hardwareIDCount > unit->SlotCount) {
        WARNING("SMMU500HardwareIDsPrepare: %u SIDs exceed the %u stream slots at %llx",
                hardwareIDCount, unit->SlotCount, unit->Common.Description.RegisterBase);
        return OS_ENOTSUPPORTED;
    }

    if (*connectionOut != NULL || space->HasConnection) {
        WARNING("SMMU500HardwareIDsPrepare: bank %u at %llx already has a connection",
                space->Bank, unit->Common.Description.RegisterBase);
        return OS_EBUSY;
    }

    status = __ValidateHardwareIDs(unit, hardwareIDs, hardwareIDCount);
    if (status != OS_EOK) {
        return status;
    }

    if (__CountFreeSlots(unit) < hardwareIDCount) {
        WARNING("SMMU500HardwareIDsPrepare: %u SIDs need more than the free stream slots at %llx",
                hardwareIDCount, unit->Common.Description.RegisterBase);
        return OS_ENOTSUPPORTED;
    }

    // Allocate the connection record before marking slots reserved so an out-of-
    // memory failure never leaves partial slot reservations behind.
    size = sizeof(SMMU500Connection_t) + hardwareIDCount * sizeof(connection->Routes[0]);
    connection = kmalloc(size);
    if (connection == NULL) {
        ERROR("SMMU500HardwareIDsPrepare: allocating %u routes for bank %u at %llx failed",
              hardwareIDCount, space->Bank, unit->Common.Description.RegisterBase);
        return OS_EOOM;
    }

    memset(connection, 0, size);
    connection->Space = space;
    connection->RouteCount = hardwareIDCount;
    __ReserveSlots(unit, connection, hardwareIDs);

    space->HasConnection = true;
    *connectionOut = connection;
    TRACE("SMMU500HardwareIDsPrepare: reserved %u SIDs for bank %u", hardwareIDCount, space->Bank);
    return OS_EOK;
}

// Program each reserved slot to match its Stream ID and route DMA traffic to
// the space's context bank. Resumes from NextRoute if an earlier call timed out.
static oserr_t
__ConnectRoutes(
    _In_ SMMU500Space_t*          space,
    _In_ SMMU500Connection_t*     connection,
    _In_ const ArmSMMUDeadline_t* deadline)
{
    SMMU500Unit_t* unit = space->Unit;
    uint32_t       slot;
    uint32_t       sid;
    oserr_t        status;

    while (connection->NextRoute < connection->RouteCount) {
        if (ArmSMMUDeadlineExpired(deadline)) {
            WARNING("__ConnectRoutes: bank %u at %llx timed out after %u of %u routes",
                    space->Bank, unit->Common.Description.RegisterBase,
                    connection->NextRoute, connection->RouteCount);
            return OS_ETIMEOUT;
        }

        slot = connection->Routes[connection->NextRoute].Slot;
        sid = connection->Routes[connection->NextRoute].SID;
        
        // S2CR already holds the fault policy from startup or an earlier
        // disconnect. Write the exact SID match into SMR first, then switch
        // S2CR to the already-enabled context bank.
        status = SMMU500WriteChecked(
            unit,
            SMMU500_SMR(slot),
            SMMU500_SMR_VALID | sid,
            SMMU500_SMR_FIELDS
        );
        if (status != OS_EOK) {
            return status;
        }

        status = SMMU500WriteChecked(
            unit,
            SMMU500_S2CR(slot),
            space->Bank,
            SMMU500_S2CR_FIELDS
        );
        if (status != OS_EOK) {
            return status;
        }
        connection->NextRoute++;
    }
    return OS_EOK;
}

oserr_t
SMMU500HardwareIDsConnect(
    _In_ void*    spaceContext,
    _In_ void*    connectionContext,
    _In_ uint64_t timeoutNs)
{
    SMMU500Space_t*      space = spaceContext;
    SMMU500Unit_t*       unit = space->Unit;
    SMMU500Connection_t* connection = connectionContext;
    ArmSMMUDeadline_t    deadline;
    oserr_t              status;

    if (connection->Space != space || connection->Phase != SMMU500ConnectionPrepared) {
        WARNING("SMMU500HardwareIDsConnect: connection of bank %u at %llx is in phase %u",
                space->Bank, unit->Common.Description.RegisterBase, (uint32_t)connection->Phase);
        return OS_EBUSY;
    }

    // Connect runs in one fixed order: enable the context bank, point the
    // stream slots at that bank, and drain a global TLB sync so no old fault
    // caching hides the new routes. Any failure is undone through Disconnect.
    connection->Phase = SMMU500ConnectionContext;
    
    status = ArmSMMUDeadlineBegin(timeoutNs, &deadline);
    if (status != OS_EOK) {
        return status;
    }

    status = SMMU500ContextEnable(space, &deadline);
    if (status != OS_EOK) {
        return status;
    }
    connection->Phase = SMMU500ConnectionRoutes;

    status = __ConnectRoutes(space, connection, &deadline);
    if (status != OS_EOK) {
        return status;
    }

    SMMU500SyncPrepare(&connection->Sync, SMMU500SyncAll, 0);
    connection->Phase = SMMU500ConnectionSync;
    
    status = SMMU500SyncRun(unit, &connection->Sync, &deadline);
    if (status != OS_EOK) {
        return status;
    }

    connection->Phase = SMMU500ConnectionConnected;
    TRACE("SMMU500HardwareIDsConnect: %u SIDs use bank %u", connection->RouteCount, space->Bank);
    return OS_EOK;
}

// Switch each connected slot back to fault mode. Resumes from NextRoute if an
// earlier call timed out.
static oserr_t
__FaultRoutes(
    _In_ SMMU500Unit_t*           unit,
    _In_ SMMU500Connection_t*     connection,
    _In_ const ArmSMMUDeadline_t* deadline)
{
    oserr_t status;

    while (connection->NextRoute < connection->RouteCount) {
        if (ArmSMMUDeadlineExpired(deadline)) {
            WARNING("__FaultRoutes: unit %llx timed out after %u of %u routes",
                    unit->Common.Description.RegisterBase, connection->NextRoute, connection->RouteCount);
            return OS_ETIMEOUT;
        }

        status = SMMU500SlotSetFault(
            unit,
            connection->Routes[connection->NextRoute].Slot
        );
        if (status != OS_EOK) {
            return status;
        }
        connection->NextRoute++;
    }
    return OS_EOK;
}

// Fault every route and drain a global TLB sync before disabling the context
// bank, saving both the route index and sync ticket across timeouts.
static oserr_t
__DisconnectRoutes(
    _In_ SMMU500Unit_t*           unit,
    _In_ SMMU500Connection_t*     connection,
    _In_ const ArmSMMUDeadline_t* deadline)
{
    oserr_t status;

    // Reset NextRoute only when first entering the faulting phase, so retries
    // after a timeout continue from the slot that timed out.
    if (connection->Phase < SMMU500ConnectionFaultRoutes) {
        connection->Phase = SMMU500ConnectionFaultRoutes;
        connection->NextRoute = 0;
    }

    if (connection->Phase == SMMU500ConnectionFaultRoutes) {
        status = __FaultRoutes(unit, connection, deadline);
        if (status != OS_EOK) {
            return status;
        }

        SMMU500SyncPrepare(&connection->Sync, SMMU500SyncAll, 0);
        connection->Phase = SMMU500ConnectionDrain;
    }

    if (connection->Phase == SMMU500ConnectionDrain) {
        status = SMMU500SyncRun(unit, &connection->Sync, deadline);
        if (status != OS_EOK) {
            return status;
        }

        connection->Phase = SMMU500ConnectionDisable;
    }
    return OS_EOK;
}

oserr_t
SMMU500HardwareIDsDisconnect(
    _In_ void*    spaceContext,
    _In_ void*    connectionContext,
    _In_ uint64_t timeoutNs)
{
    SMMU500Space_t*      space = spaceContext;
    SMMU500Unit_t*       unit = space->Unit;
    SMMU500Connection_t* connection = connectionContext;
    ArmSMMUDeadline_t    deadline;
    oserr_t              status;

    if (connection->Space != space) {
        WARNING("SMMU500HardwareIDsDisconnect: connection does not belong to bank %u at %llx",
                space->Bank, unit->Common.Description.RegisterBase);
        return OS_EINVALPARAMS;
    }

    if (connection->Phase == SMMU500ConnectionDisconnected) {
        return OS_EOK;
    }

    // If Connect was never called, no hardware slot or bank was modified.
    if (connection->Phase == SMMU500ConnectionPrepared) {
        connection->Phase = SMMU500ConnectionDisconnected;
        return OS_EOK;
    }

    status = ArmSMMUDeadlineBegin(timeoutNs, &deadline);
    if (status != OS_EOK) {
        return status;
    }

    // Wait for any earlier sync, fault and drain all routes, then turn off the
    // context bank last so in-flight translations never hit a disabled bank.
    status = SMMU500SyncWait(unit, &deadline);
    if (status != OS_EOK) {
        return status;
    }

    status = __DisconnectRoutes(unit, connection, &deadline);
    if (status != OS_EOK) {
        return status;
    }

    status = SMMU500ContextDisable(space, &deadline);
    if (status != OS_EOK) {
        return status;
    }
    
    connection->Phase = SMMU500ConnectionDisconnected;
    TRACE("SMMU500HardwareIDsDisconnect: %u SIDs left bank %u", connection->RouteCount, space->Bank);
    return OS_EOK;
}

// Verify that every slot recorded in connection is still reserved for the same
// Stream ID before releasing those slots back to the unit.
static bool
__ReservationsIntact(
    _In_ SMMU500Unit_t*       unit,
    _In_ SMMU500Connection_t* connection)
{
    for (uint32_t index = 0; index < connection->RouteCount; index++) {
        uint32_t slot = connection->Routes[index].Slot;
        if (!unit->Slots[slot].Reserved || unit->Slots[slot].SID != connection->Routes[index].SID) {
            ERROR("__ReservationsIntact: corrupt stream reservation at %llx, slot %u",
                  unit->Common.Description.RegisterBase, slot);
            return false;
        }
    }
    return true;
}

void
SMMU500HardwareIDsDestroy(
    _In_ void* spaceContext,
    _In_ void* connectionContext)
{
    SMMU500Space_t*      space = spaceContext;
    SMMU500Unit_t*       unit = space->Unit;
    SMMU500Connection_t* connection = connectionContext;

    if (unit->Common.CompletionUnreliable || connection->Space != space || !space->HasConnection ||
        (connection->Phase != SMMU500ConnectionPrepared && connection->Phase != SMMU500ConnectionDisconnected)) {
        ERROR("SMMU500HardwareIDsDestroy: destroying a live or failed connection at %llx, phase %u",
              unit->Common.Description.RegisterBase, (uint32_t)connection->Phase);
        ArmSMMUUnitFail(&unit->Common, OS_EBUSY);
        return;
    }

    if (!__ReservationsIntact(unit, connection)) {
        ArmSMMUUnitFail(&unit->Common, OS_EUNKNOWN);
        return;
    }

    for (uint32_t index = 0; index < connection->RouteCount; index++) {
        memset(&unit->Slots[connection->Routes[index].Slot], 0, sizeof(*unit->Slots));
    }

    space->HasConnection = false;
    TRACE("SMMU500HardwareIDsDestroy: released %u routes for bank %u at %llx",
          connection->RouteCount, space->Bank, unit->Common.Description.RegisterBase);
    kfree(connection);
}
