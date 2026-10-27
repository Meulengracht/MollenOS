/**
 * Copyright 2026, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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

#ifndef __DDK_INTERRUPT_PRIVATE_H__
#define __DDK_INTERRUPT_PRIVATE_H__

#include <ddk/interrupts/interrupt.h>
#include <ddk/types/interrupt-group.h>

enum __InterruptProviderOperation {
    InterruptProviderReserve,
    InterruptProviderEnable,
    InterruptProviderDisable,
    InterruptProviderRelease
};

enum __InterruptPhase {
    // Ownership may exist, but setup has not completed and cannot be enabled.
    InterruptPhasePreparing,
    // Configuration/delivery may be changed after any pending stop completes.
    InterruptPhaseReady,
    // Terminal teardown: only Disable/Destroy may advance the object.
    InterruptPhaseDestroying
};

/**
 * Independent cleanup obligations for one allocation. ProviderReserved is set
 * before RPC because a lost reply can conceal ownership. Bound includes partial
 * mappings; Enabled records successful kernel enable, not confirmed hardware
 * delivery. PendingCleanup prevents cached state from bypassing a failed stop.
 * PciMode is interpreted only by interrupt-pci.c; PlatformSource selects the
 * native source protocol without giving that source a fictitious PCI mode.
 *
 * Invariants at public-call boundaries:
 * - Enabled implies Bound; either may conservatively remain set after failure.
 * - Bound without Enabled implies PendingCleanup (including partial binding).
 * - Only Ready with no mappings or pending cleanup permits resource edits.
 * - Destroying never returns to Ready, even after a successful stop.
 * - Only successful kernel stop/unmap clears Bound and Enabled. PendingCleanup
 *   clears after that stop or after complete kernel/provider enable succeeds.
 * - ProviderReserved clears only on untouched Reserve/EBUSY or successful Release.
 */
struct DeviceInterruptState {
    uuid_t                Handle;
    int                   Bound;
    int                   Enabled;
    int                   ProviderReserved;
    uint32_t              PciMode;
    enum __InterruptPhase Phase;
    int                   PendingCleanup;
    int                   PlatformSource;
};

/**
 * @brief Common allocation/rollback entry for generic and PCI public APIs.
 * @param interrupt Initialized descriptor; must not already own an allocation.
 * @param minimum Minimum acceptable vector count.
 * @param optimal Preferred count, at least minimum.
 * @param pciModes Opaque PCI policy passed to the PCI module; unused for lines.
 * @param countOut Actual count on success; zero on failure.
 * @return Setup status; failed rollback retains State for Destroy.
 */
oserr_t
__InterruptAllocate(
    _InOut_ DeviceInterrupt_t* interrupt,
    _In_    uint32_t           minimum,
    _In_    uint32_t           optimal,
    _In_    uint32_t           pciModes,
    _Out_   uint32_t*          countOut);

/**
 * @brief Select a PCI mode and reserve its kernel resources without delivery.
 * @param interrupt PCI descriptor providing device identity and INTx routing.
 * @param minimum Minimum acceptable vector count.
 * @param optimal Preferred vector count.
 * @param modes Permitted PCI capability modes.
 * @param result Kernel reservation returned on success.
 * @param selectedMode Selected PCI capability, returned only on success.
 * @return OS_EOK or reservation/query error; no provider ownership acquired.
 */
oserr_t
__InterruptPciReserve(
    _In_  DeviceInterrupt_t*      interrupt,
    _In_  uint32_t                minimum,
    _In_  uint32_t                optimal,
    _In_  uint32_t                modes,
    _Out_ InterruptGroupResult_t* result,
    _Out_ uint32_t*               selectedMode);

/**
 * @brief Translate a shared lifecycle command into the PCI provider protocol.
 * @param interrupt Allocated PCI message object with selected mode.
 * @param operation Bus-neutral lifecycle command.
 * @return Provider/transport status, without changing local ownership flags.
 */
oserr_t
__InterruptPciControl(
    _In_ DeviceInterrupt_t*               interrupt,
    _In_ enum __InterruptProviderOperation operation);

/**
 * @brief Program every reserved PCI vector while device delivery is blocked.
 * @param interrupt PCI object whose provider reservation succeeded.
 * @return OS_EOK or query/programming error; caller must retain the lease on
 *         any failure and use the shared lifecycle to roll back partial writes.
 */
oserr_t
__InterruptPciProgram(
    _In_ DeviceInterrupt_t* interrupt);

#endif //!LIBDDK_INTERRUPT_PRIVATE_H
