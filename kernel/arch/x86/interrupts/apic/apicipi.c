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

/* Includes 
 * - System */
#include <system/utils.h>
#include <assert.h>
#include <apic.h>

#define WaitForConditionWithFault(fault, condition, runs, wait)\
fault = 0; \
for (unsigned int timeout_ = 0; !(condition); timeout_++) {\
    if (timeout_ >= runs) {\
         fault = 1; \
         break;\
                                            }\
    CpuStall(wait);\
                    }

/* ApicWaitForIcr
 * wait the ICR register to clear the BUSY bit */
void
ApicWaitForIcr(void)
{
	while (ApicReadLocal(APIC_ICR_LOW) & APIC_DELIVERY_BUSY);
}

/* This syncs the arb-ids on cross of
 * the local apic chips between the cores */
void
ApicSyncArbIds(void)
{
	// Not needed on AMD and not supported on P4 
	// So we need a check here in place to do it 
	// @todo
	ApicWaitForIcr();

	// Perform the arb-id sync
	ApicWriteLocal(APIC_ICR_HIGH,   0);
	ApicWriteLocal(APIC_ICR_LOW,    0x80000 | 0x8000 | 0x500);
	ApicWaitForIcr();
}

/* ApicSendInterrupt
 * Sends an interrupt vector-request to a given cpu-id. */
OsStatus_t
ApicSendInterrupt(
    _In_ InterruptTarget_t  Type,
    _In_ UUId_t             Specific,
    _In_ int                Vector)
{
	// Variables
	uint32_t IpiLow     = 0;
	uint32_t IpiHigh    = 0;
    UUId_t CpuId        = UUID_INVALID;

    // Get cpu-id of us
    CpuId = CpuGetCurrentId();
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

	// Physical Destination
	// Fixed Delivery
	// Assert (Bit 14 = 1)
    // Edge (Bit 15 = 0)
	IpiLow |= APIC_VECTOR(Vector) | APIC_LEVEL_ASSERT | APIC_DESTINATION_PHYSICAL;

	// Always write upper first, irq is sent when low is written
    // Before sending interrupt, make sure ICR is not busy
    // After sending interrupt, make sure ICR clears
	ApicWaitForIcr();
	ApicWriteLocal(APIC_ICR_HIGH,   IpiHigh);
	ApicWriteLocal(APIC_ICR_LOW,    IpiLow);
	ApicWaitForIcr();
    return OsSuccess;
}

/* ApicPerformIPI
 * Sends an ipi request for the specified cpu */
OsStatus_t
ApicPerformIPI(
    _In_ UUId_t             CoreId)
{
	// Variables
	uint32_t IpiLow     = APIC_DELIVERY_MODE(APIC_MODE_INIT) | APIC_LEVEL_ASSERT | APIC_DESTINATION_PHYSICAL;
	uint32_t IpiHigh    = APIC_DESTINATION(CoreId); // We use physical addressing for IPI/SIPI
	uint32_t Timeout    = 0;
    
	// Wait for ICR to clear
	ApicWaitForIcr();
    
	// Always write upper first, irq is sent when low is written
	ApicWriteLocal(APIC_ICR_HIGH,   IpiHigh);
	ApicWriteLocal(APIC_ICR_LOW,    IpiLow);
    WaitForConditionWithFault(Timeout, ((ApicReadLocal(APIC_ICR_LOW) & APIC_DELIVERY_BUSY) == 0), 200, 1);
    return (Timeout == 0) ? OsSuccess : OsError;
}

/* ApicPerformSIPI
 * Sends an sipi request for the specified cpu, to start executing code at the given vector */
OsStatus_t
ApicPerformSIPI(
    _In_ UUId_t             CoreId,
    _In_ uintptr_t          Address)
{
	// Variables
	uint32_t IpiLow     = APIC_DELIVERY_MODE(APIC_MODE_SIPI) | APIC_LEVEL_ASSERT | APIC_DESTINATION_PHYSICAL;
	uint32_t IpiHigh    = APIC_DESTINATION(CoreId); // We use physical addressing for IPI/SIPI
	uint32_t Timeout    = 0;
    uint8_t Vector      = 0;
    
    // Sanitize address given
    assert((Address % 0x1000) == 0);
    Vector = Address / 0x1000;
    assert(Vector <= 0xF);
    IpiLow |= Vector;

	// Wait for ICR to clear
	ApicWaitForIcr();
    
	// Always write upper first, irq is sent when low is written
	ApicWriteLocal(APIC_ICR_HIGH,   IpiHigh);
	ApicWriteLocal(APIC_ICR_LOW,    IpiLow);
    WaitForConditionWithFault(Timeout, ((ApicReadLocal(APIC_ICR_LOW) & APIC_DELIVERY_BUSY) == 0), 200, 1);
    return (Timeout == 0) ? OsSuccess : OsError;
}
