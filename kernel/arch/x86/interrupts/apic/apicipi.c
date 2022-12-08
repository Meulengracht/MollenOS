/**
 * Copyright 2022, Philip Meulengracht
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
 */

#include <assert.h>
#include <arch/interrupts.h>
#include <arch/utils.h>
#include <arch/x86/apic.h>
#include <component/timer.h>

#define WaitForConditionWithFault(fault, condition, runs, wait) \
    fault = OS_EOK;                                             \
    OSTimestamp_t deadline;                                     \
    SystemTimerGetWallClockTime(&deadline);                     \
    for (unsigned int tries_ = 0; !(condition); tries_++) {     \
        OSTimestampAddNsec(&deadline, &deadline, wait);         \
        SystemTimerStall(&deadline);                            \
        if (tries_ == runs - 1) {                               \
             fault = OS_ETIMEOUT;                               \
             break;                                             \
        }                                                       \
    }

static oserr_t
__WaitForAPICIdle(void)
{
    oserr_t oserr;
    WaitForConditionWithFault(
            oserr,
            (ApicReadLocal(APIC_ICR_LOW) & APIC_DELIVERY_BUSY) == 0,
            200,
            NSEC_PER_MSEC
    );
	return oserr;
}

static oserr_t
__SendIPI(
        _In_ UInteger64_t* ipi)
{
    irqstate_t irqstate;
    oserr_t    oserr;

    // Are we sending to ourselves? Handle this a bit differently, if the ICR is already
    // busy we don't want to clear it as we already an ipi pending, just queue up interrupt
    // TODO I don't see this handled
    irqstate = InterruptDisable();
    oserr    = __WaitForAPICIdle();
    if (oserr == OS_ETIMEOUT) {
        InterruptRestoreState(irqstate);
        return oserr;
    }

    ApicWriteLocal(APIC_ICR_HIGH, ipi->u.HighPart);
    ApicWriteLocal(APIC_ICR_LOW,  ipi->u.LowPart);
    oserr = __WaitForAPICIdle();
    InterruptRestoreState(irqstate);
    return oserr;
}

// This function is not needed on AMD and not supported on P4
// So we need a check here in place to do it @todo
oserr_t
ApicSynchronizeArbIds(void)
{
    UInteger64_t ipiValue;

    ipiValue.u.HighPart = 0;
    ipiValue.u.LowPart = APIC_ICR_SH_ALL | APIC_TRIGGER_LEVEL | APIC_DELIVERY_MODE(APIC_MODE_INIT);

    return __SendIPI(&ipiValue);
}

oserr_t
ApicSendInterrupt(
        _In_ InterruptTarget_t  type,
        _In_ uuid_t             specific,
        _In_ uint32_t           vector)
{
    UInteger64_t ipiValue;
    uuid_t       id  = ArchGetProcessorCoreId();

    if (type == InterruptTarget_SPECIFIC && specific == id) {
        type = InterruptTarget_SELF;
    }

    ipiValue.QuadPart = 0;

    // Handle target types
    if (type == InterruptTarget_SPECIFIC) {
        ipiValue.u.HighPart = APIC_DESTINATION(specific);
    } else if (type == InterruptTarget_SELF) {
        ipiValue.u.LowPart |= (1 << 18);
    } else if (type == InterruptTarget_ALL) {
        ipiValue.u.LowPart |= (1 << 19);
    } else {
        ipiValue.u.LowPart |= (1 << 19) | (1 << 18);
    }

	// Physical Destination (Bit 11 = 0)
	// Fixed Delivery
	// Assert (Bit 14 = 1)
    // Edge (Bit 15 = 0)
    ipiValue.u.LowPart |= APIC_VECTOR(vector) | APIC_LEVEL_ASSERT | APIC_DESTINATION_PHYSICAL;
    return __SendIPI(&ipiValue);
}

oserr_t
ApicPerformIPI(
        _In_ uuid_t coreId,
        _In_ int    assert)
{
    UInteger64_t ipiValue;

    ipiValue.u.LowPart = 0;
    ipiValue.u.HighPart = APIC_DESTINATION(coreId);

    // Determine assert or deassert
    if (assert) {
        ipiValue.u.LowPart = APIC_DELIVERY_MODE(APIC_MODE_INIT) | APIC_LEVEL_ASSERT | APIC_DESTINATION_PHYSICAL;
    } else {
        ipiValue.u.LowPart = APIC_DELIVERY_MODE(APIC_MODE_INIT) | APIC_TRIGGER_LEVEL | APIC_DESTINATION_PHYSICAL;
    }
    return __SendIPI(&ipiValue);
}

oserr_t
ApicPerformSIPI(
        _In_ uuid_t    coreId,
        _In_ uintptr_t address)
{
    UInteger64_t ipiValue;
	uint8_t      irqVector;

    ipiValue.u.LowPart = APIC_DELIVERY_MODE(APIC_MODE_SIPI) | APIC_LEVEL_ASSERT | APIC_DESTINATION_PHYSICAL;
    ipiValue.u.HighPart = APIC_DESTINATION(coreId); // We use physical addressing for IPI/SIPI

    // Sanitize address given
    assert((address % 0x1000) == 0);
    irqVector = address / 0x1000;
    assert(irqVector <= 0xF);
    ipiValue.u.LowPart |= irqVector;

    return __SendIPI(&ipiValue);
}
