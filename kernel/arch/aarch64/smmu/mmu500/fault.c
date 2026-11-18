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
 * MMU-500 fault inspection and checked register writes.
 * Recording any client fault stops new DMA mappings on the unit. When
 * hardware drops a fault report, stalls a request, or rejects a control write,
 * CompletionUnreliable latches permanently so control and data pages stay
 * allocated instead of being reused while hardware might still read them.
 */

#include "private.h"
#include <debug.h>

// MMIO register writes can stay buffered on the bus, or hardware/firmware can
// silently ignore writes to protected registers (such as locked fault routes).
// Force the write to land with a barrier, then read back and verify all bits
// selected by mask before trusting the unit.
oserr_t
SMMU500WriteChecked(
    _In_ SMMU500Unit_t* unit,
    _In_ uint32_t       offset,
    _In_ uint32_t       value,
    _In_ uint32_t       mask)
{
    uint32_t observed;

    __SMMU500Write32(unit->Common.Registers, offset, value);
    __SMMU500Barrier();
    
    observed = __SMMU500Read32(unit->Common.Registers, offset);
    if ((observed & mask) != (value & mask)) {
        ERROR("SMMU500WriteChecked: register %x at %llx rejected value %x (mask %x), read %x",
              offset, unit->Common.Description.RegisterBase, value, mask, observed);
        ArmSMMUUnitFail(&unit->Common, OS_EDEVFAULT);
        return OS_ECANCELLED;
    }
    return SMMU500FaultCheck(unit);
}

// Record a global fault on the generic DMA unit. Ordinary client faults (such
// as an unmapped Stream ID) halt new DMA work while letting teardown finish.
// Unexpected global errors (such as overflow or configuration faults) mean
// hardware state can no longer be trusted.
static void
__RecordGlobal(
    _In_ SMMU500Unit_t* unit,
    _In_ uint32_t       fault)
{
    if ((fault & SMMU500_GFSR_W1C) == 0) {
        return;
    }

    DMAIOMMUUnitFail(unit->Common.Backend, OS_EDEVFAULT);
    if ((fault & SMMU500_GFSR_W1C & ~SMMU500_GFSR_ORDINARY) != 0) {
        if (!unit->Common.CompletionUnreliable) {
            ERROR("__RecordGlobal: global fault %x at %llx makes hardware completion unreliable",
                  fault, unit->Common.Description.RegisterBase);
        }
        unit->Common.CompletionUnreliable = true;
    }
}

// Escalate unexpected context-bank faults (anything beyond ordinary address,
// permission, or access-flag faults) to CompletionUnreliable, because a
// stalled transaction or lost overflow report breaks safe resource release.
static void
__RecordContext(
    _In_ SMMU500Unit_t* unit,
    _In_ uint32_t       fault)
{
    if ((fault & SMMU500_FSR_FAULTS & ~SMMU500_FSR_ORDINARY) != 0) {
        if (!unit->Common.CompletionUnreliable) {
            ERROR("__RecordContext: context fault %x at %llx makes hardware completion unreliable",
                  fault, unit->Common.Description.RegisterBase);
        }
        unit->Common.CompletionUnreliable = true;
    }
}

oserr_t
SMMU500FaultCheck(
    _In_ SMMU500Unit_t* unit)
{
    uint32_t fault = __SMMU500Read32(unit->Common.Registers, SMMU500_GFSR);

    // Leave the status bits latched so SMMU500FaultPoll can still read the
    // matching address and syndrome registers before clearing them.
    __RecordGlobal(unit, fault);
    return unit->Common.CompletionUnreliable ? OS_ECANCELLED : OS_EOK;
}

// Read one global fault snapshot, log its address and syndromes, and clear only
// the W1C bits seen in that snapshot so racing faults stay latched.
static void
__PollGlobalFault(
    _In_ SMMU500Unit_t* unit)
{
    uint32_t fault = __SMMU500Read32(unit->Common.Registers, SMMU500_GFSR);
    uint32_t gfsynr0;
    uint32_t gfsynr1;
    uint32_t gfsynr2;
    uint64_t address;

    if ((fault & SMMU500_GFSR_W1C) == 0) {
        return;
    }

    // Read address and syndrome registers before clearing GFSR, because clearing
    // lets hardware overwrite those registers with a new fault.
    address = __SMMU500Read64(unit->Common.Registers, SMMU500_GFAR);
    gfsynr0 = __SMMU500Read32(unit->Common.Registers, SMMU500_GFSYNR0);
    gfsynr1 = __SMMU500Read32(unit->Common.Registers, SMMU500_GFSYNR1);
    gfsynr2 = __SMMU500Read32(unit->Common.Registers, SMMU500_GFSYNR2);
    __RecordGlobal(unit, fault);
    
    ERROR("__PollGlobalFault: global fault at %llx: GFSR %x, GFAR %llx, syndrome %x/%x/%x",
          unit->Common.Description.RegisterBase, fault, address, gfsynr0, gfsynr1, gfsynr2);
    __SMMU500Write32(unit->Common.Registers, SMMU500_GFSR, fault & SMMU500_GFSR_W1C);
    __SMMU500Barrier();

    // Sample GFSR once more in case another fault (or MULTI) arrived between
    // our initial read and the write-1-to-clear.
    __RecordGlobal(unit, __SMMU500Read32(unit->Common.Registers, SMMU500_GFSR));
}

// Read one context bank's fault snapshot, log the faulting Stream ID and
// address, clear only the bits we observed, and re-check for racing faults.
static void
__PollContextFault(
    _In_ SMMU500Unit_t* unit,
    _In_ uint32_t       index)
{
    uint32_t bankOffset = __SMMU500ContextBankOffset(unit, index);
    uint32_t fault = __SMMU500Read32(unit->Common.Registers, bankOffset + SMMU500_CB_FSR);
    uint32_t fsynr0;
    uint32_t cbfrsynra;
    uint64_t address;

    if ((fault & SMMU500_FSR_FAULTS) == 0) {
        return;
    }

    // Capture CB_FAR, CB_FSYNR0, and the GR1 Stream ID syndrome before W1C.
    address = __SMMU500Read64(unit->Common.Registers, bankOffset + SMMU500_CB_FAR);
    fsynr0 = __SMMU500Read32(unit->Common.Registers, bankOffset + SMMU500_CB_FSYNR0);
    cbfrsynra = __SMMU500Read32(
        unit->Common.Registers,
        __SMMU500GlobalPage1Offset(unit) + SMMU500_CBFRSYNRA(index)
    );

    // The low 16 bits of CBFRSYNRA carry the offending Stream ID.
    ERROR("__PollContextFault: context fault at %llx: bank %u, SID %x, FSR %x, FAR %llx, syndrome %x",
          unit->Common.Description.RegisterBase, index, cbfrsynra & 0xFFFFU, fault, address, fsynr0);
    DMAIOMMUUnitFail(unit->Common.Backend, OS_EDEVFAULT);
    
    __RecordContext(unit, fault);
    __SMMU500Write32(unit->Common.Registers, bankOffset + SMMU500_CB_FSR, fault & SMMU500_FSR_W1C);
    __SMMU500Barrier();
    __RecordContext(unit, __SMMU500Read32(unit->Common.Registers, bankOffset + SMMU500_CB_FSR));
}

oserr_t
SMMU500FaultPoll(
    _In_ SMMU500Unit_t* unit)
{
    __PollGlobalFault(unit);
    for (uint32_t index = 0; index < unit->ContextCount; index++) {
        __PollContextFault(unit, index);
    }
    return DMAIOMMUUnitCheck(unit->Common.Backend);
}
