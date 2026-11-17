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
 * Translated domain hardware ID connections. Reserves hardware IDs on an IOMMU
 * unit, connects them to the domain's address space, and disconnects or cleans
 * them up when the domain is discarded or destroyed.
 */

#include <assert.h>
#include <debug.h>
#include "attachment_private.h"
#include "private.h"

// Hold unit->Lock. Check that the unit is healthy before and after asking the
// backend to reserve hardware IDs. If the unit fails during preparation after
// the backend returned a record, domain->Connection stays set so the caller
// still cleans it up with DMAConnectionDestroy.
static oserr_t
__ReserveHardwareIDsLocked(
    _In_ DMATranslatedDomain_t* domain)
{
    DMAIOMMUUnit_t* unit = domain->Unit;
    oserr_t         status;

    status = DMAAddressSpaceCheck(domain->ConnectionSpace);
    if (status != OS_EOK) {
        WARNING("__ReserveHardwareIDsLocked: unit of host %u has failed: %u", domain->Host, status);
        return status;
    }

    status = unit->Ops.PrepareHardwareIDs(
        domain->ConnectionSpace->Translated.BackendSpace,
        domain->HardwareIDs,
        domain->HardwareIDCount,
        &domain->Connection
    );
    if (status != OS_EOK) {
        ERROR("__ReserveHardwareIDsLocked: hardware ID preparation for host %u failed: %u", domain->Host, status);
        return status;
    }
    assert(domain->Connection != NULL);

    status = DMAAddressSpaceCheck(domain->ConnectionSpace);
    if (status != OS_EOK) {
        WARNING("__ReserveHardwareIDsLocked: unit of host %u failed during preparation: %u", domain->Host, status);
    }
    return status;
}

oserr_t
DMAConnectionPrepare(
    _In_ DMATranslatedDomain_t* domain)
{
    struct DMAAddressSpace* space = domain->AddressSpace;
    DMAIOMMUUnit_t*         unit = domain->Unit;
    oserr_t                 status;

    // Take an independent reference on the address space before calling the
    // backend. The connection holds this reference until disconnect completes,
    // keeping page tables alive even if the creator's reference is dropped.
    assert(domain->Connection == NULL && domain->ConnectionSpace == NULL);
    status = DMAAddressSpaceAcquire(space);
    if (status != OS_EOK) {
        ERROR("DMAConnectionPrepare: space reference for host %u failed: %u", domain->Host, status);
        return status;
    }

    domain->ConnectionSpace = space;
    domain->ConnectionState = DMAConnectionPrepared;

    MutexLock(&unit->Lock);
    status = __ReserveHardwareIDsLocked(domain);
    MutexUnlock(&unit->Lock);

    if (domain->Connection == NULL) {
        DMAAddressSpaceRelease(&domain->ConnectionSpace);
        return status;
    }

    // If the unit failed right after PrepareHardwareIDs returned a record,
    // leave the software-only connection in place. The caller discards it
    // through normal cleanup.
    TRACE("DMAConnectionPrepare: host %u prepared %u hardware IDs (status %u)",
          domain->Host, domain->HardwareIDCount, status);
    return status;
}

// Hold manager->UpdateLock and manager->Lock. Verify the domain can start
// connecting to hardware. Sets *startedOut to true when the caller must call
// the backend next. Leaves *startedOut false when the domain is already
// Connected, because repeating Connect on a live domain safely does nothing.
static oserr_t
__BeginConnectLocked(
    _In_  DMATranslatedDomain_t* domain,
    _Out_ bool*                  startedOut)
{
    oserr_t status;

    *startedOut = false;
    if (domain->Manager->State != DMAManagerLive) {
        WARNING("__BeginConnectLocked: manager %u is closed, host %u not connected",
                domain->Manager->ID, domain->Host);
        return OS_ECANCELLED;
    }

    if (domain->State != DMATranslatedDomainReady) {
        WARNING("__BeginConnectLocked: host %u is not ready, state %u",
                domain->Host, (uint32_t)domain->State);
        return OS_EBUSY;
    }

    status = DMAAddressSpaceCheck(domain->AddressSpace);
    if (status != OS_EOK) {
        WARNING("__BeginConnectLocked: unit of host %u has failed: %u", domain->Host, status);
        return status;
    }

    if (domain->ConnectionState == DMAConnectionConnected) {
        // Repeating a successful Connect does nothing, even after devices join.
        return OS_EOK;
    }

    if (domain->ConnectionState != DMAConnectionPrepared ||
        domain->Connection == NULL || domain->Members != 0) {
        WARNING("__BeginConnectLocked: host %u cannot connect, connection state %u, members %u",
                domain->Host, (uint32_t)domain->ConnectionState, domain->Members);
        return OS_EBUSY;
    }

    domain->ConnectionState = DMAConnectionConnecting;
    *startedOut = true;
    return OS_EOK;
}

// Program the hardware routes under unit->Lock, without holding manager locks
// so manager close cannot stall on hardware waits. Any failure leaves the state
// at Connecting so Destroy will still disconnect partially programmed routes.
static oserr_t
__ConnectHardwareIDs(
    _In_ DMATranslatedDomain_t* domain)
{
    DMAIOMMUUnit_t* unit = domain->Unit;
    oserr_t         status;

    MutexLock(&unit->Lock);
    status = DMAAddressSpaceCheck(domain->ConnectionSpace);
    if (status != OS_EOK) {
        WARNING("__ConnectHardwareIDs: unit of host %u has failed: %u", domain->Host, status);
        goto unlock;
    }

    status = unit->Ops.ConnectHardwareIDs(
        domain->ConnectionSpace->Translated.BackendSpace,
        domain->Connection,
        unit->Config.CompletionTimeoutNs
    );
    if (status != OS_EOK) {
        ERROR("__ConnectHardwareIDs: host %u connection failed: %u", domain->Host, status);
        goto unlock;
    }

    status = DMAAddressSpaceCheck(domain->ConnectionSpace);
    if (status != OS_EOK) {
        WARNING("__ConnectHardwareIDs: unit of host %u failed during connect: %u", domain->Host, status);
    }

unlock:
    MutexUnlock(&unit->Lock);
    return status;
}

// Mark the domain Connected only if the manager, domain, and unit stayed
// healthy while the backend call ran outside manager locks. If anything closed
// or failed in the meantime, ConnectionState stays at Connecting so Destroy
// disconnects the hardware routes before freeing the address space.
static oserr_t
__FinishConnect(
    _In_ DMATranslatedDomain_t* domain)
{
    DMAManager_t* manager = domain->Manager;
    oserr_t       status;

    MutexLock(&manager->UpdateLock);
    MutexLock(&manager->Lock);
    if (manager->State != DMAManagerLive || domain->State != DMATranslatedDomainReady) {
        WARNING("__FinishConnect: manager %u or host %u closed while connecting",
                manager->ID, domain->Host);
        status = OS_ECANCELLED;
    } else {
        status = DMAAddressSpaceCheck(domain->ConnectionSpace);
        if (status == OS_EOK) {
            domain->ConnectionState = DMAConnectionConnected;
            TRACE("__FinishConnect: host %u connected %u hardware IDs", domain->Host, domain->HardwareIDCount);
        } else {
            WARNING("__FinishConnect: unit of host %u failed after connect: %u", domain->Host, status);
        }
    }
    MutexUnlock(&manager->Lock);
    MutexUnlock(&manager->UpdateLock);
    return status;
}

oserr_t
DMATranslatedDomainConnect(
    _In_ DMATranslatedDomain_t* domain)
{
    DMAManager_t* manager;
    bool          started;
    oserr_t       status;
    oserr_t       propagationStatus;

    if (domain == NULL) {
        ERROR("DMATranslatedDomainConnect: domain is NULL");
        return OS_EINVALPARAMS;
    }
    manager = domain->Manager;

    // Flow:
    // 1. Under UpdateLock and Lock, verify the domain can connect and move it
    //    to Connecting. UpdateLock blocks concurrent attachment preparation.
    // 2. Drop both manager locks and call the backend under unit->Lock, so
    //    closing the manager never waits on hardware timeouts.
    // 3. Re-acquire the manager locks and mark the domain Connected if nothing
    //    closed or failed during step 2.
    // 4. Scan the manager's domains and block recovery groups on any failed unit,
    //    catching failures that happened during or just around this call.
    MutexLock(&manager->UpdateLock);
    MutexLock(&manager->Lock);
    status = __BeginConnectLocked(domain, &started);
    MutexUnlock(&manager->Lock);
    MutexUnlock(&manager->UpdateLock);

    if (status == OS_EOK && started) {
        status = __ConnectHardwareIDs(domain);
        if (status == OS_EOK) {
            status = __FinishConnect(domain);
        }
    }

    propagationStatus = DMARecoveryBlockFailedUnits(manager);
    if (propagationStatus != OS_EOK) {
        ERROR("DMATranslatedDomainConnect: failure propagation failed: %u (connection %u)",
              propagationStatus, status);
    }
    return status;
}

// Hold unit->Lock. Disconnect hardware IDs and verify the unit stayed healthy
// afterward. A failed unit cannot prove that cached routes or in-flight table
// lookups have stopped, so we leave the connection intact.
static oserr_t
__DisconnectHardwareIDsLocked(
    _In_ DMATranslatedDomain_t* domain)
{
    DMAIOMMUUnit_t* unit = domain->Unit;
    oserr_t         status;

    status = DMAAddressSpaceCheck(domain->ConnectionSpace);
    if (status != OS_EOK) {
        WARNING("__DisconnectHardwareIDsLocked: failed unit preserves host %u hardware ID state: %u",
                domain->Host, status);
        return status;
    }

    domain->ConnectionState = DMAConnectionDisconnecting;
    status = unit->Ops.DisconnectHardwareIDs(
        domain->ConnectionSpace->Translated.BackendSpace,
        domain->Connection,
        unit->Config.CompletionTimeoutNs
    );
    if (status != OS_EOK) {
        ERROR("__DisconnectHardwareIDsLocked: host %u disconnect failed: %u", domain->Host, status);
        return status;
    }

    status = DMAAddressSpaceCheck(domain->ConnectionSpace);
    if (status != OS_EOK) {
        WARNING("__DisconnectHardwareIDsLocked: unit of host %u failed during disconnect: %u",
                domain->Host, status);
    }
    return status;
}

oserr_t
DMAConnectionDestroy(
    _In_ DMATranslatedDomain_t* domain)
{
    DMAIOMMUUnit_t* unit = domain->Unit;
    oserr_t         status;

    if (domain->Connection == NULL) {
        assert(domain->ConnectionSpace == NULL);
        return OS_EOK;
    }

    MutexLock(&unit->Lock);
    if (domain->ConnectionState != DMAConnectionPrepared &&
        domain->ConnectionState != DMAConnectionDisconnected) {
        // Hardware routes may already exist (Connecting, Connected, or an
        // earlier incomplete Disconnecting). Disconnect them before freeing
        // any tracking records.
        status = __DisconnectHardwareIDsLocked(domain);
        if (status != OS_EOK) {
            MutexUnlock(&unit->Lock);
            return status;
        }
    }

    // Either preparation never touched hardware (safe even on a failed unit),
    // or disconnect just proved every hardware ID is unreachable. Free the
    // backend record and clear Connection under unit->Lock so a later retry
    // cannot repeat cleanup, then drop ConnectionSpace outside unit->Lock
    // because releasing the last space reference acquires unit->Lock.
    domain->ConnectionState = DMAConnectionDisconnected;
    unit->Ops.DestroyHardwareIDs(domain->ConnectionSpace->Translated.BackendSpace, domain->Connection);
    domain->Connection = NULL;
    MutexUnlock(&unit->Lock);

    DMAAddressSpaceRelease(&domain->ConnectionSpace);
    TRACE("DMAConnectionDestroy: host %u hardware IDs released", domain->Host);
    return OS_EOK;
}
