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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS x86 Advanced Programmable Interrupt Controller Driver
 *  - Ipi and synchronization utility functions
 */

#include <system/interrupts.h>
#include <system/utils.h>
#include <system/time.h>
#include <assert.h>
#include <apic.h>

#define WaitForConditionWithFault(fault, condition, runs, wait)\
fault = 0; \
for (unsigned int timeout_ = 0; !(condition); timeout_++) {\
    if (timeout_ >= runs) {\
         fault = 1; \
         break;\
                                            }\
    ArchStallProcessorCore(wait);\
                    }

/* ApicWaitForIdle
 * Waits for the ICR delivery busy bit to clear, to make sure an ICR is succesfully delivered to the core. */
OsStatus_t
ApicWaitForIdle(void)
{
    int Error = 0;
    WaitForConditionWithFault(Error, (ApicReadLocal(APIC_ICR_LOW) & APIC_DELIVERY_BUSY) == 0, 200, 1);
	return (Error == 0) ? OsSuccess : OsError;
}

/* ApicSynchronizeArbIds
 * Performs a INIT-Deassert to perform the synchronization of the arbitration ids on all cores. */
void
ApicSynchronizeArbIds(void)
{
    // Variables
    IntStatus_t InterruptStatus;
    OsStatus_t Status;

	// Not needed on AMD and not supported on P4 
	// So we need a check here in place to do it 
	// @todo
    InterruptStatus = InterruptDisable();
	Status          = ApicWaitForIdle();
    assert(Status != OsError);

	ApicWriteLocal(APIC_ICR_HIGH,   0);
	ApicWriteLocal(APIC_ICR_LOW,    APIC_ICR_SH_ALL | APIC_TRIGGER_LEVEL | APIC_DELIVERY_MODE(APIC_MODE_INIT));
	
    Status = ApicWaitForIdle();
    assert(Status != OsError);
    InterruptRestoreState(InterruptStatus);
}

/* ApicSendInterrupt
 * Sends an interrupt vector-request to a given cpu-id. */
OsStatus_t
ApicSendInterrupt(
    _In_ InterruptTarget_t  Type,
    _In_ UUId_t             Specific,
    _In_ int                Vector)
{
	uint32_t    IpiLow  = 0;
	uint32_t    IpiHigh = 0;
    UUId_t      CpuId   = UUID_INVALID;
    IntStatus_t InterruptStatus;
    OsStatus_t  Status;
    
    // Get cpu-id of us
    CpuId = ArchGetProcessorCoreId();
    if (Type == InterruptSpecific && Specific == CpuId) {
        Type = InterruptSelf;
    }

    // Handle target types
    if (Type == InterruptSpecific) {
	    IpiHigh = APIC_DESTINATION(Specific);
    }
    else if (Type == InterruptSelf) {
        IpiLow |= (1 << 18);
    }
    else if (Type == InterruptAll) {
        IpiLow |= (1 << 19);
    }
    else {
        IpiLow |= (1 << 19) | (1 << 18);
    }

	// Physical Destination (Bit 11 = 0)
	// Fixed Delivery
	// Assert (Bit 14 = 1)
    // Edge (Bit 15 = 0)
	IpiLow |= APIC_VECTOR(Vector) | APIC_LEVEL_ASSERT | APIC_DESTINATION_PHYSICAL;

    // Are we sending to ourself? Handle this a bit differently, if the ICR is already
    // busy we don't want to clear it as we already a send pending, just queue up interrupt
    InterruptStatus = InterruptDisable();
	Status          = ApicWaitForIdle();
    if (Status == OsSuccess) {
        ApicWriteLocal(APIC_ICR_HIGH, IpiHigh);
        ApicWriteLocal(APIC_ICR_LOW,  IpiLow);
    }
    InterruptRestoreState(InterruptStatus);
    return Status;
}

/* ApicPerformIPI
 * Sends an ipi request for the specified cpu */
OsStatus_t
ApicPerformIPI(
    _In_ UUId_t             CoreId,
    _In_ int                Assert)
{
	uint32_t    IpiHigh = APIC_DESTINATION(CoreId); // We use physical addressing for IPI/SIPI
	uint32_t    IpiLow  = 0;
    IntStatus_t InterruptStatus;
    OsStatus_t  Status;

    // Determine assert or deassert
    if (Assert) {
        IpiLow = APIC_DELIVERY_MODE(APIC_MODE_INIT) | APIC_LEVEL_ASSERT | APIC_DESTINATION_PHYSICAL;
    }
    else {
        IpiLow = APIC_DELIVERY_MODE(APIC_MODE_INIT) | APIC_TRIGGER_LEVEL | APIC_DESTINATION_PHYSICAL;
    }
    
	// Wait for ICR to clear
    InterruptStatus = InterruptDisable();
	Status          = ApicWaitForIdle();
    if (Status == OsSuccess) {        
        // Always write upper first, irq is sent when low is written
        ApicWriteLocal(APIC_ICR_HIGH, IpiHigh);
        ApicWriteLocal(APIC_ICR_LOW,  IpiLow);
        Status = ApicWaitForIdle(); 
    }
    InterruptRestoreState(InterruptStatus);
    return Status;
}

/* ApicPerformSIPI
 * Sends an sipi request for the specified cpu, to start executing code at the given vector */
OsStatus_t
ApicPerformSIPI(
    _In_ UUId_t             CoreId,
    _In_ uintptr_t          Address)
{
	uint32_t    IpiLow  = APIC_DELIVERY_MODE(APIC_MODE_SIPI) | APIC_LEVEL_ASSERT | APIC_DESTINATION_PHYSICAL;
	uint32_t    IpiHigh = APIC_DESTINATION(CoreId); // We use physical addressing for IPI/SIPI
    uint8_t     Vector  = 0;
    IntStatus_t InterruptStatus;
    OsStatus_t  Status;
    
    // Sanitize address given
    assert((Address % 0x1000) == 0);
    Vector = Address / 0x1000;
    assert(Vector <= 0xF);
    IpiLow |= Vector;

	// Wait for ICR to clear
    InterruptStatus = InterruptDisable();
	Status          = ApicWaitForIdle();
    if (Status == OsSuccess) {        
        // Always write upper first, irq is sent when low is written
        ApicWriteLocal(APIC_ICR_HIGH, IpiHigh);
        ApicWriteLocal(APIC_ICR_LOW,  IpiLow);
        Status = ApicWaitForIdle();
    }
    InterruptRestoreState(InterruptStatus);
    return Status;
}
