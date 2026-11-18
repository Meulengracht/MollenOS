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
 * SMMUv3 thread-context error checks and event ring diagnostics.
 * Reads global hardware error flags and drains fault records from the event
 * ring without using interrupts. Reporting or clearing a fault never means
 * devices have stopped issuing DMA.
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

// When the event ring fills faster than software drains it, hardware toggles
// the overflow bit (OVFLG) in EVTQ_PROD and drops incoming fault records.
// Acknowledge that bit in EventConsumer so we can still read the records left
// in the ring for debugging, while marking the unit permanently failed. If
// available exceeds capacity, the hardware ring pointers cannot be trusted.
static oserr_t
__AcknowledgeEventOverflow(
    _In_ SMMUv3Unit_t* unit,
    _In_ uint32_t      producer,
    _In_ uint32_t      available,
    _In_ uint32_t      capacity)
{
    uint32_t mask = (capacity * 2U) - 1U;

    ERROR("__AcknowledgeEventOverflow: event queue overflow at %llx, producer %x, consumer %x",
          unit->Common.Description.RegisterBase, producer, unit->EventConsumer);
    
    ArmSMMUUnitFail(&unit->Common, OS_EDEVFAULT);
    if (available > capacity) {
        return OS_ECANCELLED;
    }
    unit->EventConsumer = (unit->EventConsumer & mask) | (producer & SMMUV3_EVENT_OVERFLOW);
    return OS_EOK;
}

// Log one 32-byte event record and fail the generic DMA unit. Copying all
// four 64-bit words locally keeps the log lines consistent.
static void
__ReportEvent(
    _In_ SMMUv3Unit_t*   unit,
    _In_ const uint64_t* event)
{
    uint64_t words[SMMUV3_EVENT_WORDS];
    uint32_t type;

    for (uint32_t index = 0; index < SMMUV3_EVENT_WORDS; index++) {
        words[index] = event[index];
    }

    type = (uint32_t)words[0] & SMMUV3_EVENT_TYPE_MASK;
    ERROR("__ReportEvent: fault at %llx, SID %u, event %x, words %llx/%llx/%llx",
          unit->Common.Description.RegisterBase, (uint32_t)(words[0] >> SMMUV3_EVENT_SID_SHIFT), type,
          words[1], words[2], words[3]);

    // Translation fault codes (0x10..0x13) store the failing device DMA address
    // in word 2. Other event types are logged as raw words above.
    if (type >= SMMUV3_EVENT_ADDRESS_FIRST && type <= SMMUV3_EVENT_ADDRESS_LAST) {
        ERROR("__ReportEvent: fault address %llx", words[2]);
    }

    // A stalled transaction pauses inside the SMMU until software sends a
    // resume or terminate command. Because this driver never sends those
    // commands, hardware cleanup and command completion can no longer be trusted.
    if ((words[1] & SMMUV3_EVENT_STALL) != 0) {
        ERROR("__ReportEvent: stalled transaction at %llx prevents safe cleanup",
              unit->Common.Description.RegisterBase);
        unit->Common.CompletionUnreliable = true;
    }
    DMAIOMMUUnitFail(unit->Common.Backend, OS_EDEVFAULT);
}

// Read and log count event records from the event ring. The caller writes the
// updated EventConsumer to EVTQ_CONS afterward, keeping the overflow toggle
// bit intact while advancing the ring index and wrap bit.
static void
__ConsumeEvents(
    _In_ SMMUv3Unit_t* unit,
    _In_ uint32_t      count)
{
    uint32_t        capacity = 1U << unit->EventBits;
    uint32_t        mask = (capacity * 2U) - 1U;
    uint32_t        index;
    const uint64_t* event;

    __SMMUv3Barrier();
    for (index = 0; index < count; index++) {
        event = (const uint64_t*)unit->Events.VirtualAddress +
                ((unit->EventConsumer & (capacity - 1U)) * SMMUV3_EVENT_WORDS);
        __ReportEvent(unit, event);
        unit->EventConsumer = (unit->EventConsumer & SMMUV3_EVENT_OVERFLOW) |
                              ((unit->EventConsumer + 1U) & mask);
    }
}

oserr_t
SMMUv3FaultDrain(
    _In_ SMMUv3Unit_t* unit,
    _In_ uint32_t      maximumEvents)
{
    uint32_t capacity = 1U << unit->EventBits;
    uint32_t mask = (capacity * 2U) - 1U;
    uint32_t producer;
    uint32_t available;
    uint32_t count;
    bool     overflow;
    oserr_t  status;

    status = SMMUv3FaultCheck(unit);
    if (status != OS_EOK) {
        // Keep reading event records even after failure so diagnostics show up
        // in the log. Nothing here clears the failure or unpins control memory.
        WARNING("SMMUv3FaultDrain: draining events from failed unit at %llx",
                unit->Common.Description.RegisterBase);
    }

    // Ring indices carry a wrap bit above the slot index, so the pending count
    // is taken modulo 2 * capacity. Bit 31 toggles whenever events overflow.
    producer = __SMMUv3Read32(unit->Common.Registers, SMMUV3_EVTQ_PROD);
    available = ((producer & mask) - (unit->EventConsumer & mask)) & mask;
    overflow = ((producer ^ unit->EventConsumer) & SMMUV3_EVENT_OVERFLOW) != 0;
    if (overflow || available > capacity) {
        status = __AcknowledgeEventOverflow(unit, producer, available, capacity);
        if (status != OS_EOK) {
            return status;
        }
    }

    count = available;
    if (count > maximumEvents) {
        count = maximumEvents;
    }

    __ConsumeEvents(unit, count);
    if (count != 0 || overflow) {
        // Complete all event reads from RAM before advancing EVTQ_CONS so the
        // controller cannot overwrite unread entries.
        __SMMUv3Barrier();
        __SMMUv3Write32(unit->Common.Registers, SMMUV3_EVTQ_CONS, unit->EventConsumer);
    }
    return DMAIOMMUUnitCheck(unit->Common.Backend);
}
