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
#include <apic.h>

/* ApicWaitForIcr
 * wait the ICR register to clear the BUSY bit */
void
ApicWaitForIcr(void)
{
	while (ApicReadLocal(APIC_ICR_LOW) & APIC_ICR_BUSY)
		;
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
	ApicWriteLocal(APIC_ICR_HIGH, 0);
	ApicWriteLocal(APIC_ICR_LOW, 0x80000 | 0x8000 | 0x500);
	ApicWaitForIcr();
}

/* ApicSendInterrupt
 * Sends an interrupt vector-request to a given cpu-id. */
OsStatus_t
ApicSendInterrupt(
    _In_ InterruptTarget_t Type,
    _In_ UUId_t Specific,
    _In_ int Vector)
{
	// Variables
    UUId_t CpuId        = UUID_INVALID;
	uint32_t IpiLow     = 0;
	uint32_t IpiHigh    = 0;

    // Get cpu-id of us
    CpuId = CpuGetCurrentId();
    if (Type == InterruptSpecific && Specific == CpuId) {
        Type = InterruptSelf;
    }

    // Handle target types
    if (Type == InterruptSpecific) {
	    IpiHigh |= ((ApicGetCpuMask(Specific) << 24));
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
	IpiLow |= (Vector & 0xFF);
	IpiLow |= (1 << 11);
	IpiLow |= (1 << 14);

	// Wait for ICR to clear
	ApicWaitForIcr();

	// Always write upper first, irq is sent when low is written
	ApicWriteLocal(APIC_ICR_HIGH, IpiHigh);
	ApicWriteLocal(APIC_ICR_LOW, IpiLow);
    return OsSuccess;
}
