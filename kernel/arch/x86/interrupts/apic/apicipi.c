/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 *
 * Advanced Programmable Interrupt Controller Driver
 *  - Ipi and synchronization utility functions
 */
#define __MODULE "APIC"
#define __TRACE

#include <arch/interrupts.h>
#include <arch/utils.h>
#include <arch/x86/apic.h>
#include <assert.h>
#include <component/timer.h>
#include <debug.h>

#define WaitForConditionWithFault(fault, condition, runs, wait)\
fault = 0; \
for (unsigned int timeout_ = 0; !(condition); timeout_++) {\
    if (timeout_ >= (runs)) {\
         (fault) = 1; \
         break;\
                                            }\
    SystemTimerStall(wait * NSEC_PER_MSEC);\
                    }

oscode_t
ApicWaitForIdle(void)
{
    int Error = 0;
    WaitForConditionWithFault(Error, (ApicReadLocal(APIC_ICR_LOW) & APIC_DELIVERY_BUSY) == 0, 200, 1);
	return (Error == 0) ? OsOK : OsError;
}

void
ApicSynchronizeArbIds(void)
{
    IntStatus_t interruptStatus;
    oscode_t  osStatus;

	// Not needed on AMD and not supported on P4 
	// So we need a check here in place to do it @todo
    interruptStatus = InterruptDisable();
    osStatus        = ApicWaitForIdle();
    assert(osStatus != OsError);

	ApicWriteLocal(APIC_ICR_HIGH,   0);
	ApicWriteLocal(APIC_ICR_LOW,    APIC_ICR_SH_ALL | APIC_TRIGGER_LEVEL | APIC_DELIVERY_MODE(APIC_MODE_INIT));

    osStatus = ApicWaitForIdle();
    assert(osStatus != OsError);
    InterruptRestoreState(interruptStatus);
}

oscode_t
ApicSendInterrupt(
    _In_ InterruptTarget_t  type,
    _In_ UUId_t             specific,
    _In_ uint32_t           vector)
{
	uint32_t    IpiLow  = 0;
	uint32_t    IpiHigh = 0;
    UUId_t      CoreId  = ArchGetProcessorCoreId();
    IntStatus_t InterruptStatus;
    oscode_t  Status;

    if (type == InterruptTarget_SPECIFIC && specific == CoreId) {
        type = InterruptTarget_SELF;
    }

    // Handle target types
    if (type == InterruptTarget_SPECIFIC) {
	    IpiHigh = APIC_DESTINATION(specific);
    }
    else if (type == InterruptTarget_SELF) {
        IpiLow |= (1 << 18);
    }
    else if (type == InterruptTarget_ALL) {
        IpiLow |= (1 << 19);
    }
    else {
        IpiLow |= (1 << 19) | (1 << 18);
    }

	// Physical Destination (Bit 11 = 0)
	// Fixed Delivery
	// Assert (Bit 14 = 1)
    // Edge (Bit 15 = 0)
	IpiLow |= APIC_VECTOR(vector) | APIC_LEVEL_ASSERT | APIC_DESTINATION_PHYSICAL;

    // Are we sending to ourself? Handle this a bit differently, if the ICR is already
    // busy we don't want to clear it as we already a send pending, just queue up interrupt
    InterruptStatus = InterruptDisable();
	Status          = ApicWaitForIdle();
    if (Status == OsOK) {
        ApicWriteLocal(APIC_ICR_HIGH, IpiHigh);
        ApicWriteLocal(APIC_ICR_LOW,  IpiLow);
        Status = ApicWaitForIdle();
    }
    InterruptRestoreState(InterruptStatus);
    return Status;
}

oscode_t
ApicPerformIPI(
    _In_ UUId_t coreId,
    _In_ int    assert)
{
	uint32_t    IpiHigh = APIC_DESTINATION(coreId); // We use physical addressing for IPI/SIPI
	uint32_t    IpiLow  = 0;
    IntStatus_t InterruptStatus;
    oscode_t  Status;

    // Determine assert or deassert
    if (assert) {
        IpiLow = APIC_DELIVERY_MODE(APIC_MODE_INIT) | APIC_LEVEL_ASSERT | APIC_DESTINATION_PHYSICAL;
    }
    else {
        IpiLow = APIC_DELIVERY_MODE(APIC_MODE_INIT) | APIC_TRIGGER_LEVEL | APIC_DESTINATION_PHYSICAL;
    }
    
	// Wait for ICR to clear
    InterruptStatus = InterruptDisable();
	Status          = ApicWaitForIdle();
    if (Status == OsOK) {
        // Always write upper first, irq is sent when low is written
        ApicWriteLocal(APIC_ICR_HIGH, IpiHigh);
        ApicWriteLocal(APIC_ICR_LOW,  IpiLow);
        Status = ApicWaitForIdle(); 
    }
    InterruptRestoreState(InterruptStatus);
    return Status;
}

oscode_t
ApicPerformSIPI(
    _In_ UUId_t    coreId,
    _In_ uintptr_t Address)
{
	uint32_t    IpiLow  = APIC_DELIVERY_MODE(APIC_MODE_SIPI) | APIC_LEVEL_ASSERT | APIC_DESTINATION_PHYSICAL;
	uint32_t    IpiHigh = APIC_DESTINATION(coreId); // We use physical addressing for IPI/SIPI
    uint8_t     Vector  = 0;
    IntStatus_t InterruptStatus;
    oscode_t  Status;
    
    // Sanitize address given
    assert((Address % 0x1000) == 0);
    Vector = Address / 0x1000;
    assert(Vector <= 0xF);
    IpiLow |= Vector;

	// Wait for ICR to clear
    InterruptStatus = InterruptDisable();
	Status          = ApicWaitForIdle();
    if (Status == OsOK) {
        // Always write upper first, irq is sent when low is written
        ApicWriteLocal(APIC_ICR_HIGH, IpiHigh);
        ApicWriteLocal(APIC_ICR_LOW,  IpiLow);
        Status = ApicWaitForIdle();
    }
    InterruptRestoreState(InterruptStatus);
    return Status;
}
