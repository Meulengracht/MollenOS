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
 * The unit records faults in status registers instead of a queue. GFSR holds
 * faults that belong to no context bank, and each context bank has its own
 * CB_FSR. A status register keeps the first fault until software clears it.
 * Each fault is sorted into one of two classes:
 * - A device fault: the unit refused an access that a device sent. Poll turns
 *   it into a DMAIOMMUFault_t record for the caller, and the unit stays healthy.
 * - A unit failure: the fault points at a table or register that the kernel
 *   wrote, or at the unit itself. The unit is failed permanently through
 *   ArmSMMUUnitFail, which keeps control memory allocated.
 * Only Poll clears fault bits, and it clears a device fault only after writing
 * its record. Every other check leaves the bits latched for Poll.
 */

#include "private.h"
#include <debug.h>

// Optional fields of a device fault record.
#define __FAULT_ADDRESS   0x1U // The fault address register holds the address the device used.
#define __FAULT_DIRECTION 0x2U // The syndrome register holds the direction (WNR) of the access.

/**
 * @brief Describes one fault status bit that is caused by what a device sent.
 *
 * Kind is the fault kind reported to the caller. Fields is a mask of the
 * __FAULT_* bits above. Status bits missing from these tables fail the unit.
 */
typedef struct __SMMU500DeviceFault {
    uint32_t               Bit;
    enum DMAIOMMUFaultKind Kind;
    uint32_t               Fields;
} __SMMU500DeviceFault_t;

// Global device faults. Each one describes a device access that reached no
// context bank. No address space is involved, so the record kind is NoRoute
// for all three. Section numbers refer to the SMMU v2 specification.
// - USF: no stream match slot accepted the Stream ID.
// - ICF: the Stream ID matched a slot whose route (S2CR) is set to fault
//   (section 3.8). SMMU500SlotSetFault and __ConnectRoutes in stream.c leave
//   a slot like that for a moment between their two register writes. A device
//   that sends DMA in that moment raises ICF. The kernel never routes a stream
//   to a context bank it has not set up, so ICF is read as "a device sent DMA
//   while its route was closed". The cost: a kernel bug that does route a
//   stream to a badly set up bank also raises ICF. It is then reported as
//   NoRoute and does not fail the unit. A barrier or other transaction that
//   needs no translation also goes through stream matching (section 3.8.3),
//   so a device can raise ICF without sending DMA. GFAR then holds no DMA
//   address, but the record cannot tell the two apart.
// - UUT: the device sent a kind of transaction the unit does not support.
//
// A global fault that arrives while GFSR is zero also records GFAR, GFSYNR0
// and GFSYNR1 (section 3.8.1), so USF and ICF records carry the address,
// direction and Stream ID. For an unsupported transaction, section 3.8.1
// names only GFSR. That record keeps the Stream ID from GFSYNR1 and leaves
// out the address and direction.
//
// When one status has more than one of these bits set, the first entry in
// table order picks the record fields. Entries with an address come first,
// so the record with the most detail is kept.
//
// Every other GFSR bit fails the unit. SMCF, UCBF and UCIF point at stream
// routes or context bank settings the kernel wrote. CAF means the kernel
// accessed a register that does not exist. EF is an external abort.
static const __SMMU500DeviceFault_t g_globalFaults[] = {
    { SMMU500_GFSR_USF, DMAIOMMUFaultNoRoute, __FAULT_ADDRESS | __FAULT_DIRECTION },
    { SMMU500_GFSR_ICF, DMAIOMMUFaultNoRoute, __FAULT_ADDRESS | __FAULT_DIRECTION },
    { SMMU500_GFSR_UUT, DMAIOMMUFaultNoRoute, 0 }
};

// Context bank device faults, in the order used to pick the kind when one
// snapshot has more than one bit set.
//
// Every other FSR bit fails the unit. AFF reports a page table entry with its
// access flag clear, but the kernel sets that flag in every stage 2 entry it
// writes. EF is an external abort, for example during a page table walk.
// TLBMCF and TLBLKF are problems inside the unit's own TLB. SS means a
// transaction is stalled. This driver never stalls (CB_SCTLR.CFCFG is 0) and
// never writes CB_RESUME, so a stalled transaction could wait forever.
//
// ASF has two causes. The unit checks every output address against the
// physical address size: an address from a page table entry, from a table
// walk, or the root in CB_TTBR0. The kernel wrote all of those, so that case
// fails the unit. For stage 2 only translation, the unit may also check the
// input address. When CB_FAR lies above the input address range, the device
// sent that address and the fault stays a device fault. See __AddressSizeFromTable.
static const __SMMU500DeviceFault_t g_contextFaults[] = {
    { SMMU500_FSR_TF,  DMAIOMMUFaultNoMapping,    __FAULT_ADDRESS | __FAULT_DIRECTION },
    { SMMU500_FSR_PF,  DMAIOMMUFaultPermission,   __FAULT_ADDRESS | __FAULT_DIRECTION },
    { SMMU500_FSR_ASF, DMAIOMMUFaultAddressRange, __FAULT_ADDRESS | __FAULT_DIRECTION },
    { SMMU500_FSR_UUT, DMAIOMMUFaultOther,        __FAULT_ADDRESS | __FAULT_DIRECTION }
};

/**
 * @brief Register values of one recorded fault, read before the status is cleared.
 *
 * Clearing the status lets hardware record the next fault, which overwrites
 * the address and syndrome registers. All decisions, the record and the log
 * line use this one copy. Status is GFSR or CB_FSR. Syndrome is GFSYNR0 or
 * CB_FSYNR0. StreamID comes from GFSYNR1 or CBFRSYNRA. Address is GFAR or
 * CB_FAR. Write is the WNR bit of the syndrome.
 */
typedef struct __SMMU500FaultSnapshot {
    uint32_t Status;
    uint32_t Syndrome;
    uint32_t StreamID;
    uint64_t Address;
    bool     Write;
} __SMMU500FaultSnapshot_t;

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

// Returns the device fault entry of the first table bit set in status, or
// NULL when no bit of the table is set. Each table holds exactly the bits of
// its ORDINARY mask. The failure reason helpers treat a missing entry as a
// unit failure, so a record is only filled from a valid entry.
static const __SMMU500DeviceFault_t*
__FindDeviceFault(
    _In_ const __SMMU500DeviceFault_t* table,
    _In_ uint32_t                      count,
    _In_ uint32_t                      status)
{
    for (uint32_t index = 0; index < count; index++) {
        if ((status & table[index].Bit) != 0) {
            return &table[index];
        }
    }
    return NULL;
}

// Returns why a GFSR value fails the unit, or NULL when it is a device fault.
// entry is the result of __FindDeviceFault for status. MULTI together with
// USF, ICF or UUT is a device fault. The lost reports came from Stream IDs
// that reached no context bank, and Poll returns the one record.
static const char*
__GlobalFailureReason(
    _In_ const __SMMU500DeviceFault_t* entry,
    _In_ uint32_t                      status)
{
    if ((status & SMMU500_GFSR_W1C & ~(SMMU500_GFSR_ORDINARY | SMMU500_GFSR_MULTI)) != 0) {
        return "global fault from kernel routes, unit registers or an external abort";
    }

    // Only MULTI is left, so nothing shows who sent the lost accesses.
    if (entry == NULL) {
        return "global faults lost without a recorded fault";
    }
    return NULL;
}

// Returns true when an address size fault cannot have come from the device
// address. Stage 2 input addresses up to Stage2.AddressLimit are valid, so a
// fault on one of them was raised by an output address from a kernel table.
static bool
__AddressSizeFromTable(
    _In_ const SMMU500Unit_t*            unit,
    _In_ const __SMMU500FaultSnapshot_t* snapshot)
{
    return snapshot->Address <= unit->Stage2.AddressLimit;
}

// Returns why a context bank fault fails the unit, or NULL when it is a
// device fault. entry is the result of __FindDeviceFault for the status in
// snapshot. MULTI together with a device fault bit is a device fault. The
// bank serves one address space, so the lost faults belong to the domain of
// the recorded one.
static const char*
__ContextFailureReason(
    _In_ const SMMU500Unit_t*            unit,
    _In_ const __SMMU500DeviceFault_t*   entry,
    _In_ const __SMMU500FaultSnapshot_t* snapshot)
{
    // A stalled transaction waits inside the unit until software writes
    // CB_RESUME. This driver never does, so the unit cannot finish its work.
    if ((snapshot->Status & SMMU500_FSR_SS) != 0) {
        return "stalled transaction";
    }

    if ((snapshot->Status & SMMU500_FSR_FAULTS & ~(SMMU500_FSR_ORDINARY | SMMU500_FSR_MULTI)) != 0) {
        return "context fault from kernel tables or the unit itself";
    }

    // Only MULTI is left, so nothing describes the lost faults.
    if (entry == NULL) {
        return "context faults lost without a recorded fault";
    }

    if ((snapshot->Status & SMMU500_FSR_ASF) != 0 && __AddressSizeFromTable(unit, snapshot)) {
        return "page table entry points outside the output address size";
    }
    return NULL;
}

// Read the global fault registers into snapshot. status is the GFSR value the
// caller read first. The other registers describe the fault it shows.
static void
__ReadGlobalFault(
    _In_  SMMU500Unit_t*            unit,
    _In_  uint32_t                  status,
    _Out_ __SMMU500FaultSnapshot_t* snapshot)
{
    snapshot->Status = status;
    snapshot->Address = __SMMU500Read64(unit->Common.Registers, SMMU500_GFAR);
    snapshot->Syndrome = __SMMU500Read32(unit->Common.Registers, SMMU500_GFSYNR0);
    snapshot->StreamID = __SMMU500Read32(unit->Common.Registers, SMMU500_GFSYNR1) & SMMU500_GFSYNR1_SID;
    snapshot->Write = (snapshot->Syndrome & SMMU500_GFSYNR0_WRITE) != 0;
}

// Read the fault registers of one context bank into snapshot. status is the
// CB_FSR value the caller read first. CBFRSYNRA lives in Global Register
// Space 1, the others in the bank's own register page.
static void
__ReadContextFault(
    _In_  SMMU500Unit_t*            unit,
    _In_  uint32_t                  bank,
    _In_  uint32_t                  status,
    _Out_ __SMMU500FaultSnapshot_t* snapshot)
{
    uint32_t bankOffset = __SMMU500ContextBankOffset(unit, bank);
    uint32_t syndrome;

    snapshot->Status = status;
    snapshot->Address = __SMMU500Read64(unit->Common.Registers, bankOffset + SMMU500_CB_FAR);
    snapshot->Syndrome = __SMMU500Read32(unit->Common.Registers, bankOffset + SMMU500_CB_FSYNR0);
    syndrome = __SMMU500Read32(
        unit->Common.Registers,
        __SMMU500GlobalPage1Offset(unit) + SMMU500_CBFRSYNRA(bank)
    );
    snapshot->StreamID = syndrome & SMMU500_CBFRSYNRA_SID;
    snapshot->Write = (snapshot->Syndrome & SMMU500_FSYNR0_WRITE) != 0;
}

// Log a global fault that fails the unit, then fail it. The log line carries
// the raw registers so the cause can be found. GFSYNR2 is implementation
// defined and is read only for this line.
static void
__FailForGlobalFault(
    _In_ SMMU500Unit_t*                  unit,
    _In_ const __SMMU500FaultSnapshot_t* snapshot,
    _In_ const char*                     reason)
{
    uint32_t extra = __SMMU500Read32(unit->Common.Registers, SMMU500_GFSYNR2);

    ERROR("__FailForGlobalFault: %s at %llx, SID %u, GFSR %x, GFAR %llx, GFSYNR0 %x, GFSYNR2 %x",
          reason, unit->Common.Description.RegisterBase, snapshot->StreamID, snapshot->Status,
          snapshot->Address, snapshot->Syndrome, extra);
    ArmSMMUUnitFail(&unit->Common, OS_EDEVFAULT);
}

// Log a context bank fault that fails the unit, then fail it. The log line
// names the bank and carries the raw registers so the cause can be found.
static void
__FailForContextFault(
    _In_ SMMU500Unit_t*                  unit,
    _In_ uint32_t                        bank,
    _In_ const __SMMU500FaultSnapshot_t* snapshot,
    _In_ const char*                     reason)
{
    ERROR("__FailForContextFault: %s at %llx, bank %u, SID %u, FSR %x, FAR %llx, FSYNR0 %x",
          reason, unit->Common.Description.RegisterBase, bank, snapshot->StreamID,
          snapshot->Status, snapshot->Address, snapshot->Syndrome);
    ArmSMMUUnitFail(&unit->Common, OS_EDEVFAULT);
}

// Fill a fault record from a device fault. Address and direction are only
// copied when the entry says the registers hold them. Code is the raw status.
static void
__FillDeviceFault(
    _In_  const __SMMU500DeviceFault_t*   entry,
    _In_  const __SMMU500FaultSnapshot_t* snapshot,
    _Out_ DMAIOMMUFault_t*                fault)
{
    fault->Kind = entry->Kind;
    fault->Flags = 0;
    fault->HardwareID = snapshot->StreamID;
    fault->Code = snapshot->Status;
    fault->DeviceAddress = 0;

    if ((entry->Fields & __FAULT_ADDRESS) != 0) {
        fault->Flags |= DMA_IOMMU_FAULT_ADDRESS;
        fault->DeviceAddress = snapshot->Address;
    }

    if ((entry->Fields & __FAULT_DIRECTION) != 0) {
        fault->Flags |= snapshot->Write ? DMA_IOMMU_FAULT_WRITE : DMA_IOMMU_FAULT_READ;
    }
}

// Write clear to a write-one-to-clear status register at offset and wait for
// the write to reach the unit before the next status read.
//
// Callers clear only the bits seen in their snapshot, so a fault that arrives
// after the read stays latched. MULTI is the exception for a device fault.
// Hardware sets MULTI only while the reported bit is still set, so a MULTI
// that arrived after the read belongs to the fault just reported. Left alone,
// it would read as lost faults with no recorded fault, which fails the unit.
static void
__ClearStatus(
    _In_ SMMU500Unit_t* unit,
    _In_ uint32_t       offset,
    _In_ uint32_t       clear)
{
    __SMMU500Write32(unit->Common.Registers, offset, clear);
    __SMMU500Barrier();
}

oserr_t
SMMU500FaultCheck(
    _In_ SMMU500Unit_t* unit)
{
    __SMMU500FaultSnapshot_t      snapshot;
    const __SMMU500DeviceFault_t* entry;
    uint32_t                      status;
    const char*                   reason;

    // A failed unit stays failed, so there is nothing more to decide.
    if (unit->Common.CompletionUnreliable) {
        return OS_ECANCELLED;
    }

    // Device faults stay latched so Poll can return their records. Only a
    // unit failure is acted on here.
    status = __SMMU500Read32(unit->Common.Registers, SMMU500_GFSR);
    if ((status & SMMU500_GFSR_W1C) == 0) {
        return OS_EOK;
    }

    entry = __FindDeviceFault(g_globalFaults, SIZEOF_ARRAY(g_globalFaults), status);
    reason = __GlobalFailureReason(entry, status);
    if (reason != NULL) {
        __ReadGlobalFault(unit, status, &snapshot);
        __FailForGlobalFault(unit, &snapshot, reason);
        return OS_ECANCELLED;
    }
    return OS_EOK;
}

// Check one context bank for a unit failure without clearing anything.
static void
__CheckContextFault(
    _In_ SMMU500Unit_t* unit,
    _In_ uint32_t       bank)
{
    uint32_t                      bankOffset = __SMMU500ContextBankOffset(unit, bank);
    __SMMU500FaultSnapshot_t      snapshot;
    const __SMMU500DeviceFault_t* entry;
    uint32_t                      status;
    const char*                   reason;

    status = __SMMU500Read32(unit->Common.Registers, bankOffset + SMMU500_CB_FSR);
    if ((status & SMMU500_FSR_FAULTS) == 0) {
        return;
    }

    __ReadContextFault(unit, bank, status, &snapshot);
    entry = __FindDeviceFault(g_contextFaults, SIZEOF_ARRAY(g_contextFaults), status);
    reason = __ContextFailureReason(unit, entry, &snapshot);
    if (reason != NULL) {
        __FailForContextFault(unit, bank, &snapshot, reason);
    }
}

oserr_t
SMMU500FaultCheckAll(
    _In_ SMMU500Unit_t* unit)
{
    oserr_t status;

    status = SMMU500FaultCheck(unit);
    if (status != OS_EOK) {
        return status;
    }

    for (uint32_t bank = 0; bank < unit->ContextCount && !unit->Common.CompletionUnreliable; bank++) {
        __CheckContextFault(unit, bank);
    }
    return DMAIOMMUUnitCheck(unit->Common.Backend);
}

// Read and clear the global fault status. A device fault fills fault and
// returns true. A unit failure is logged, fails the unit and returns false.
static bool
__PollGlobalFault(
    _In_  SMMU500Unit_t*   unit,
    _Out_ DMAIOMMUFault_t* fault)
{
    __SMMU500FaultSnapshot_t      snapshot;
    const __SMMU500DeviceFault_t* entry;
    uint32_t                      status;
    const char*                   reason;

    status = __SMMU500Read32(unit->Common.Registers, SMMU500_GFSR);
    if ((status & SMMU500_GFSR_W1C) == 0) {
        return false;
    }

    __ReadGlobalFault(unit, status, &snapshot);
    entry = __FindDeviceFault(g_globalFaults, SIZEOF_ARRAY(g_globalFaults), status);
    reason = __GlobalFailureReason(entry, status);
    if (reason != NULL) {
        __FailForGlobalFault(unit, &snapshot, reason);
        __ClearStatus(unit, SMMU500_GFSR, status & SMMU500_GFSR_W1C);
        return false;
    }

    __FillDeviceFault(entry, &snapshot, fault);
    __ClearStatus(unit, SMMU500_GFSR, (status & SMMU500_GFSR_W1C) | SMMU500_GFSR_MULTI);
    return true;
}

// Read and clear the fault status of one context bank. A device fault fills
// fault and returns true. A unit failure is logged, fails the unit and
// returns false. A bank without a fault returns false and is not written.
static bool
__PollContextFault(
    _In_  SMMU500Unit_t*   unit,
    _In_  uint32_t         bank,
    _Out_ DMAIOMMUFault_t* fault)
{
    uint32_t                      statusOffset = __SMMU500ContextBankOffset(unit, bank) + SMMU500_CB_FSR;
    __SMMU500FaultSnapshot_t      snapshot;
    const __SMMU500DeviceFault_t* entry;
    uint32_t                      status;
    const char*                   reason;

    status = __SMMU500Read32(unit->Common.Registers, statusOffset);
    if ((status & SMMU500_FSR_FAULTS) == 0) {
        return false;
    }

    __ReadContextFault(unit, bank, status, &snapshot);
    entry = __FindDeviceFault(g_contextFaults, SIZEOF_ARRAY(g_contextFaults), status);
    reason = __ContextFailureReason(unit, entry, &snapshot);
    if (reason != NULL) {
        __FailForContextFault(unit, bank, &snapshot, reason);
        __ClearStatus(unit, statusOffset, status & SMMU500_FSR_W1C);
        return false;
    }

    __FillDeviceFault(entry, &snapshot, fault);
    __ClearStatus(unit, statusOffset, (status & SMMU500_FSR_W1C) | SMMU500_FSR_MULTI);
    return true;
}

oserr_t
SMMU500FaultPoll(
    _In_  SMMU500Unit_t*   unit,
    _Out_ DMAIOMMUFault_t* faults,
    _In_  uint32_t         capacity,
    _Out_ uint32_t*        countOut)
{
    uint32_t count = 0;

    // The global record goes first. capacity is at least 1, so it always fits.
    if (__PollGlobalFault(unit, &faults[count])) {
        count++;
    }

    // Banks are read in index order. Once faults is full, the remaining banks
    // are not read, so their status stays latched for the next call.
    for (uint32_t bank = 0; bank < unit->ContextCount && count < capacity; bank++) {
        if (__PollContextFault(unit, bank, &faults[count])) {
            count++;
        }
    }

    *countOut = count;
    return DMAIOMMUUnitCheck(unit->Common.Backend);
}
