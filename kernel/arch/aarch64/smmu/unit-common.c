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
 * ARM SMMU Common Unit Lifecycle
 * Shared startup, polling, teardown, and failure-latch gates for SMMUv3 and
 * MMU-500 hardware units.
 */

#include "unit-common.h"
#include "../common/device.h"
#include <debug.h>

oserr_t
ArmSMMUUnitCommonInitialize(
    _In_ ArmSMMUUnitCommon_t*        common,
    _In_ const ArmSMMUDescription_t* description)
{
    common->Description = *description;
    
    atomic_init(&common->State, ArmSMMUUnitProbed);
    list_construct(&common->Spaces);

    // Map the controller's MMIO window as persistent Device memory so later
    // steps can read and write registers without touching hardware here.
    common->Registers = Arm64MapDevice(
        description->RegisterBase,
        description->RegisterLength
    );
    if (common->Registers == 0) {
        ERROR("ArmSMMUUnitCommonInitialize: mapping registers at %llx failed", description->RegisterBase);
        return OS_EOOM;
    }
    return OS_EOK;
}

oserr_t
ArmSMMUUnitStart(
    _In_ ArmSMMUUnitCommon_t* common,
    _In_ ArmSMMUUnitStep_t    start,
    _In_ void*                unit)
{
    enum ArmSMMUUnitState expected = ArmSMMUUnitProbed;
    oserr_t               status;

    // Only one thread can win the transition from Probed to Starting. Late
    // callers read the state left behind by that winner instead of racing on
    // hardware setup.
    if (!atomic_compare_exchange_strong(&common->State, &expected, ArmSMMUUnitStarting)) {
        if (expected == ArmSMMUUnitReady) {
            return DMAIOMMUUnitCheck(common->Backend);
        }
        WARNING("ArmSMMUUnitStart: unit %llx cannot start from state %u",
                common->Description.RegisterBase, (uint32_t)expected);
        return expected == ArmSMMUUnitStartFailed ? OS_ECANCELLED : OS_EBUSY;
    }

    TRACE("ArmSMMUUnitStart: starting unit %llx", common->Description.RegisterBase);
    DMAIOMMUUnitLock(common->Backend);
    status = start(unit);
    atomic_store(&common->State, status == OS_EOK ? ArmSMMUUnitReady : ArmSMMUUnitStartFailed);
    DMAIOMMUUnitUnlock(common->Backend);
    TRACE("ArmSMMUUnitStart: unit %llx finished with status %u", common->Description.RegisterBase, status);
    return status;
}

oserr_t
ArmSMMUUnitPoll(
    _In_ ArmSMMUUnitCommon_t* common,
    _In_ ArmSMMUUnitStep_t    poll,
    _In_ void*                unit)
{
    enum ArmSMMUUnitState state;
    oserr_t               status;

    DMAIOMMUUnitLock(common->Backend);
    state = atomic_load(&common->State);
    if (state == ArmSMMUUnitProbed || state == ArmSMMUUnitStarting || state == ArmSMMUUnitClosing) {
        DMAIOMMUUnitUnlock(common->Backend);
        return OS_EBUSY;
    }

    // Still let poll inspect and clear faults after a failed start, while
    // converting a clean poll result into OS_EBUSY so the unit stays unusable.
    status = poll(unit);
    if (status == OS_EOK && state == ArmSMMUUnitStartFailed) {
        status = OS_EBUSY;
    }
    DMAIOMMUUnitUnlock(common->Backend);
    return status;
}

oserr_t
ArmSMMUUnitDestroy(
    _In_ ArmSMMUUnitCommon_t* common,
    _In_ ArmSMMUUnitStep_t    teardown,
    _In_ void*                unit)
{
    oserr_t status;

    DMAIOMMUUnitLock(common->Backend);
    if (common->Spaces.count != 0 || atomic_load(&common->State) == ArmSMMUUnitStarting) {
        WARNING("ArmSMMUUnitDestroy: unit %llx is still in use, %i spaces",
                common->Description.RegisterBase, common->Spaces.count);
        DMAIOMMUUnitUnlock(common->Backend);
        return OS_EBUSY;
    }

    // Move to Closing while holding the unit lock so concurrent polls stay out
    // even if teardown fails and needs a retry.
    atomic_store(&common->State, ArmSMMUUnitClosing);

    // Hardware may still be reading page tables or queues when completions fail
    // or the memory pool is broken, so keep all control memory allocated.
    if (common->CompletionUnreliable || common->Memory.State == ArmSMMUMemoryFailed) {
        ERROR("ArmSMMUUnitDestroy: unit %llx is pinned because hardware completion cannot be trusted",
              common->Description.RegisterBase);
        DMAIOMMUUnitUnlock(common->Backend);
        return OS_ECANCELLED;
    }

    status = teardown(unit);
    DMAIOMMUUnitUnlock(common->Backend);
    if (status != OS_EOK) {
        return status;
    }
    
    status = DMAIOMMUUnitDestroy(&common->Backend);
    if (status != OS_EOK) {
        ERROR("ArmSMMUUnitDestroy: generic unit destruction at %llx failed: %u",
              common->Description.RegisterBase, status);
        return status;
    }
    TRACE("ArmSMMUUnitDestroy: unit %llx destroyed", common->Description.RegisterBase);
    return OS_EOK;
}

oserr_t
ArmSMMUUnitFail(
    _In_ ArmSMMUUnitCommon_t* common,
    _In_ oserr_t              status)
{
    common->CompletionUnreliable = true;
    DMAIOMMUUnitFail(common->Backend, status);
    return status;
}

void
ArmSMMUUnitStage2Fail(
    _In_ void*   context,
    _In_ oserr_t status)
{
    ArmSMMUUnitFail(context, status);
}

bool
ArmSMMUUnitIsReady(
    _In_ ArmSMMUUnitCommon_t* common)
{
    return atomic_load(&common->State) == ArmSMMUUnitReady;
}
