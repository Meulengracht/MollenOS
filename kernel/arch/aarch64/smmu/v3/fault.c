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
 * SMMUv3 thread-context error checks and event queue handling.
 * Reads global hardware error flags and drains the event queue without using
 * interrupts. Each event record is sorted into one of two classes:
 * - A device fault: the unit refused an access that a device sent. It becomes
 *   a DMAIOMMUFault_t record for the caller, and the unit stays healthy.
 * - A unit failure: the event points at a table or queue that the kernel wrote,
 *   or at memory the unit reads by itself. The unit is failed permanently.
 * Reporting or clearing a fault never means devices have stopped issuing DMA.
 */

#include "private.h"
#include <debug.h>

oserr_t
SMMUv3FaultCheck(
    _In_ SMMUv3Unit_t* unit)
{
    uint32_t errors;

    // Hardware toggles a bit in GERROR when a fatal controller error occurs,
    // and software acknowledges it by matching that bit in GERRORN. Any bit
    // where GERROR and GERRORN differ is an active unacknowledged error. This
    // backend does not repair command errors or reset a failed controller, so
    // any active bit permanently fails the unit.
    errors = __SMMUv3Read32(unit->Common.Registers, SMMUV3_GERROR) ^
             __SMMUv3Read32(unit->Common.Registers, SMMUV3_GERRORN);
    if (errors != 0) {
        if (!unit->Common.CompletionUnreliable) {
            ERROR("SMMUv3FaultCheck: global error at %llx: %x", unit->Common.Description.RegisterBase, errors);
        }
        ArmSMMUUnitFail(&unit->Common, OS_EDEVFAULT);
    }
    return unit->Common.CompletionUnreliable ? OS_ECANCELLED : OS_EOK;
}

// Optional fields of an event record. Section 7.3 of the specification defines
// which fields each event number carries. The same bits hold other data in
// event numbers that do not define the field, so only read the listed ones.
#define __EVENT_ADDRESS     0x1U // Word 2 holds the address the device used.
#define __EVENT_DIRECTION   0x2U // Word 1 holds RnW, the direction of the access.
#define __EVENT_STALL       0x4U // Word 1 holds Stall.
#define __EVENT_STAGE1_ONLY 0x8U // Only a stage 1 fault is a device fault. S2 set fails the unit.

/**
 * @brief Describes one event number that is caused by what a device sent.
 *
 * Kind is the fault kind reported to the caller. Fields is a mask of the
 * __EVENT_* bits above. Event numbers missing from g_deviceEvents fail the unit.
 */
typedef struct __SMMUv3DeviceEvent {
    uint32_t               Number;
    enum DMAIOMMUFaultKind Kind;
    uint32_t               Fields;
} __SMMUv3DeviceEvent_t;

// Device fault events. Each one describes an access that the unit refused and
// ended, so no memory was touched and nothing waits inside the unit. A record
// with Stall set is the exception and fails the unit, see __UnitFailureReason.
//
// Events that are left out fail the unit. Those are reads of the stream table,
// context descriptors, page tables or virtual machine structures that went
// wrong (0x03, 0x04, 0x09, 0x0A, 0x0B, 0x25), cache conflicts inside the unit
// (0x20, 0x21), page request hints that this driver does not handle (0x24),
// and every unknown or implementation defined number.
//
// F_ACCESS (0x12) is left out as well. It reports a page table entry with its
// access flag clear. The kernel sets that flag in every entry it writes, so
// the event can only mean that a page table is damaged.
//
// F_ADDR_SIZE has two causes in a stage 2 only setup. With S2 clear, the
// device used an address wider than the unit's input size (SMMU_IDR5.IAS).
// With S2 set, a page table entry the kernel wrote points outside the output
// size in STE.S2PS, so that case fails the unit.
static const __SMMUv3DeviceEvent_t g_deviceEvents[] = {
    { SMMUV3_EVENT_F_UUT,              DMAIOMMUFaultOther,
      __EVENT_ADDRESS | __EVENT_DIRECTION },
    { SMMUV3_EVENT_C_BAD_STREAMID,     DMAIOMMUFaultNoRoute,      0 },
    // The address of an ATS request has bits [11:0] cleared by the unit.
    { SMMUV3_EVENT_F_BAD_ATS_TREQ,     DMAIOMMUFaultOther,        __EVENT_ADDRESS },
    { SMMUV3_EVENT_F_STREAM_DISABLED,  DMAIOMMUFaultOther,        0 },
    { SMMUV3_EVENT_F_TRANSL_FORBIDDEN, DMAIOMMUFaultOther,
      __EVENT_ADDRESS | __EVENT_DIRECTION },
    { SMMUV3_EVENT_C_BAD_SUBSTREAMID,  DMAIOMMUFaultOther,        0 },
    { SMMUV3_EVENT_F_TRANSLATION,      DMAIOMMUFaultNoMapping,
      __EVENT_ADDRESS | __EVENT_DIRECTION | __EVENT_STALL },
    { SMMUV3_EVENT_F_ADDR_SIZE,        DMAIOMMUFaultAddressRange,
      __EVENT_ADDRESS | __EVENT_DIRECTION | __EVENT_STALL | __EVENT_STAGE1_ONLY },
    { SMMUV3_EVENT_F_PERMISSION,       DMAIOMMUFaultPermission,
      __EVENT_ADDRESS | __EVENT_DIRECTION | __EVENT_STALL }
};

// Returns the device fault entry for an event number, or NULL if the number
// is not a device fault.
static const __SMMUv3DeviceEvent_t*
__FindDeviceEvent(
    _In_ uint32_t number)
{
    for (uint32_t index = 0; index < sizeof(g_deviceEvents) / sizeof(g_deviceEvents[0]); index++) {
        if (g_deviceEvents[index].Number == number) {
            return &g_deviceEvents[index];
        }
    }
    return NULL;
}

// Returns why an event record fails the unit, or NULL when the record is a
// device fault. entry is the result of __FindDeviceEvent for the record.
static const char*
__UnitFailureReason(
    _In_ const __SMMUv3DeviceEvent_t* entry,
    _In_ const uint64_t*              words)
{
    if (entry == NULL) {
        return "event from kernel tables, unit memory or an unknown event number";
    }

    // A stalled transaction waits inside the unit until software sends a
    // resume or terminate command. This driver never sends those commands, so
    // the unit can no longer be trusted to finish its work.
    if ((entry->Fields & __EVENT_STALL) != 0 && (words[1] & SMMUV3_EVENT_STALL) != 0) {
        return "stalled transaction";
    }

    if ((entry->Fields & __EVENT_STAGE1_ONLY) != 0 && (words[1] & SMMUV3_EVENT_STAGE2) != 0) {
        return "page table entry points outside the output address size";
    }
    return NULL;
}

// Log an event record that fails the unit, then fail it. The log line carries
// the event number, the Stream ID and the raw words so the cause can be found.
static void
__FailForEvent(
    _In_ SMMUv3Unit_t*   unit,
    _In_ const uint64_t* words,
    _In_ const char*     reason)
{
    ERROR("__FailForEvent: %s at %llx, event %x, SID %u, words %llx/%llx/%llx",
          reason, unit->Common.Description.RegisterBase, (uint32_t)words[0] & SMMUV3_EVENT_TYPE_MASK,
          (uint32_t)(words[0] >> SMMUV3_EVENT_SID_SHIFT), words[1], words[2], words[3]);
    ArmSMMUUnitFail(&unit->Common, OS_EDEVFAULT);
}

// Fill a fault record from a device fault event. Address and direction are
// only copied when the event number defines them.
static void
__FillDeviceFault(
    _In_  const __SMMUv3DeviceEvent_t* entry,
    _In_  const uint64_t*              words,
    _Out_ DMAIOMMUFault_t*             fault)
{
    fault->Kind = entry->Kind;
    fault->Flags = 0;
    fault->HardwareID = (uint32_t)(words[0] >> SMMUV3_EVENT_SID_SHIFT);
    fault->Code = (uint32_t)words[0] & SMMUV3_EVENT_TYPE_MASK;
    fault->DeviceAddress = 0;

    if ((entry->Fields & __EVENT_ADDRESS) != 0) {
        fault->Flags |= DMA_IOMMU_FAULT_ADDRESS;
        fault->DeviceAddress = words[SMMUV3_EVENT_ADDRESS_WORD];
    }

    if ((entry->Fields & __EVENT_DIRECTION) != 0) {
        fault->Flags |= (words[1] & SMMUV3_EVENT_READ) != 0 ? DMA_IOMMU_FAULT_READ : DMA_IOMMU_FAULT_WRITE;
    }
}

// Sort one 32-byte event record. A device fault fills fault and returns true.
// Any other record fails the unit, leaves fault untouched and returns false.
static bool
__HandleEvent(
    _In_  SMMUv3Unit_t*    unit,
    _In_  const uint64_t*  event,
    _Out_ DMAIOMMUFault_t* fault)
{
    uint64_t                     words[SMMUV3_EVENT_WORDS];
    const __SMMUv3DeviceEvent_t* entry;
    const char*                  reason;

    // Copy the record out of the queue once, so the checks, the record and
    // the log line all see the same values.
    for (uint32_t index = 0; index < SMMUV3_EVENT_WORDS; index++) {
        words[index] = event[index];
    }

    entry = __FindDeviceEvent((uint32_t)words[0] & SMMUV3_EVENT_TYPE_MASK);
    reason = __UnitFailureReason(entry, words);
    if (reason != NULL) {
        __FailForEvent(unit, words, reason);
        return false;
    }

    __FillDeviceFault(entry, words, fault);
    return true;
}

// Read count event records from the queue and return how many fault records
// were written to faults. faults must have room for count records. The caller
// writes the updated EventConsumer to EVTQ_CONS afterward, which keeps the
// overflow bit intact while advancing the queue index and wrap bit.
static uint32_t
__ConsumeEvents(
    _In_  SMMUv3Unit_t*    unit,
    _In_  uint32_t         count,
    _Out_ DMAIOMMUFault_t* faults)
{
    uint32_t        queueSize = 1U << unit->EventBits;
    uint32_t        mask = (queueSize * 2U) - 1U;
    uint32_t        written = 0;
    const uint64_t* event;

    // Order the EVTQ_PROD read before the reads of the records it published.
    __SMMUv3Barrier();
    for (uint32_t index = 0; index < count; index++) {
        event = (const uint64_t*)unit->Events.VirtualAddress +
                ((unit->EventConsumer & (queueSize - 1U)) * SMMUV3_EVENT_WORDS);
        if (__HandleEvent(unit, event, &faults[written])) {
            written++;
        }
        unit->EventConsumer = (unit->EventConsumer & SMMUV3_EVENT_OVERFLOW) |
                              ((unit->EventConsumer + 1U) & mask);
    }
    return written;
}

// Acknowledge an event queue overflow and describe it in fault. Hardware
// toggles the overflow bit (OVFLG) in EVTQ_PROD when it drops new records
// because the queue is full. Copying that bit into EventConsumer tells
// hardware the loss was seen once EVTQ_CONS is written. Records still in the
// queue are intact and are read as usual. The lost records may belong to any
// Stream ID, so the record names none. Code holds the EVTQ_PROD value.
static void
__AcknowledgeEventOverflow(
    _In_  SMMUv3Unit_t*    unit,
    _In_  uint32_t         producer,
    _Out_ DMAIOMMUFault_t* fault)
{
    unit->EventConsumer = (unit->EventConsumer & ~SMMUV3_EVENT_OVERFLOW) | (producer & SMMUV3_EVENT_OVERFLOW);

    fault->Kind = DMAIOMMUFaultReportsLost;
    fault->Flags = 0;
    fault->HardwareID = 0;
    fault->Code = producer;
    fault->DeviceAddress = 0;
}

oserr_t
SMMUv3FaultDrain(
    _In_  SMMUv3Unit_t*    unit,
    _Out_ DMAIOMMUFault_t* faults,
    _In_  uint32_t         capacity,
    _Out_ uint32_t*        countOut)
{
    uint32_t queueSize = 1U << unit->EventBits;
    uint32_t mask = (queueSize * 2U) - 1U;
    uint32_t producer;
    uint32_t available;
    uint32_t events;
    uint32_t count = 0;
    bool     overflow;
    oserr_t  status;

    *countOut = 0;
    status = SMMUv3FaultCheck(unit);
    if (status != OS_EOK) {
        // Keep reading event records after a failure so diagnostics show up
        // in the log. Nothing here clears the failure or unpins control memory.
        WARNING("SMMUv3FaultDrain: draining events from failed unit at %llx",
                unit->Common.Description.RegisterBase);
    }

    // Queue indices carry a wrap bit above the slot index, so the pending
    // count is taken modulo 2 * queueSize. More pending records than slots
    // means the hardware pointers are broken and cannot be trusted.
    producer = __SMMUv3Read32(unit->Common.Registers, SMMUV3_EVTQ_PROD);
    available = ((producer & mask) - (unit->EventConsumer & mask)) & mask;
    if (available > queueSize) {
        ERROR("SMMUv3FaultDrain: event queue pointers at %llx are invalid, producer %x, consumer %x",
              unit->Common.Description.RegisterBase, producer, unit->EventConsumer);
        ArmSMMUUnitFail(&unit->Common, OS_EDEVFAULT);
        return OS_ECANCELLED;
    }

    // An overflow takes the first slot. With capacity 1 it is the only record
    // of this call, and every queued event waits for the next call. That way
    // the overflow notice is never dropped and the queue keeps its records.
    overflow = ((producer ^ unit->EventConsumer) & SMMUV3_EVENT_OVERFLOW) != 0;
    if (overflow) {
        __AcknowledgeEventOverflow(unit, producer, &faults[0]);
        count = 1;
    }

    // Read no more events than there are free slots. Each event writes at
    // most one record, and the rest stay queued for the next call.
    events = available;
    if (events > capacity - count) {
        events = capacity - count;
    }
    count += __ConsumeEvents(unit, events, &faults[count]);

    if (events != 0 || overflow) {
        // Complete all event reads from RAM before advancing EVTQ_CONS so the
        // controller cannot overwrite unread entries.
        __SMMUv3Barrier();
        __SMMUv3Write32(unit->Common.Registers, SMMUV3_EVTQ_CONS, unit->EventConsumer);
    }

    *countOut = count;
    return DMAIOMMUUnitCheck(unit->Common.Backend);
}
