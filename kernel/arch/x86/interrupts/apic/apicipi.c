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
#include <apic.h>
#include <acpi.h>
#include <log.h>

/* Includes 
 * - C-Library */
#include <ds/list.h>

/* Externs, we need access to the
 * acpi nodes */
__EXTERN List_t *GlbAcpiNodes;

/* Utility function wait for 
 * wait the ICR register to clear
 * the BUSY bit */
void ApicWaitForIcr(void)
{
	while (ApicReadLocal(APIC_ICR_LOW) & APIC_ICR_BUSY)
		;
}

/* This syncs the arb-ids on cross of
 * the local apic chips between the cores */
void ApicSyncArbIds(void)
{
	/* Not needed on AMD and not supported on P4 
	 * So we need a check here in place to do it 
	 * TODO */

	/* First, we wait for ICR to clear */
	ApicWaitForIcr();

	/* Perform the arb-id sync */
	ApicWriteLocal(APIC_ICR_HIGH, 0);
	ApicWriteLocal(APIC_ICR_LOW, 0x80000 | 0x8000 | 0x500);

	/* Wait for the ICR to clear again */
	ApicWaitForIcr();
}

/* Invoke an IPI request on either target
 * cpu, or on all cpu cores if a broadcast
 * has been requested. The supplied vector will
 * be the invoked interrupt */
void ApicSendIpiLoop(void *ApicInfo, int n, void *IrqVector)
{
	/* Cast and derive parameters */
	ACPI_MADT_LOCAL_APIC *Core = 
		(ACPI_MADT_LOCAL_APIC*)ApicInfo;
	uint32_t tVector = *(uint32_t*)IrqVector;
	
	/* Variables for the IPI */
	uint32_t IpiLow = 0;
	uint32_t IpiHigh = 0;

	/* Structure of IO Entry Register:
	 * Bits 0 - 7: Interrupt Vector that will be raised (Valid ranges are from 0x10 - 0xFE) - Read/Write
	 * Bits 8 - 10: Delivery Mode. - Read / Write
	 *      - 000: Fixed Delivery, deliver interrupt to all cores listed in destination.
	 *      - 001: Lowest Priority, deliver interrupt to a core running lowest priority.
	 *      - 010: System Management Interrupt, must be edge triggered.
	 *      - 011: Reserved
	 *      - 100: NMI, deliver the interrupt to NMI signal of all cores, must be edge triggered.
	 *      - 101: INIT (Init Core)
	 *      - 110: SIPI (Start-Up)
	 *      - 111: ExtINT, Like fixed, requires edge triggered.
	 * Bit 11: Destination Mode, determines how the destination is interpreted. 0 means
	 *                           phyiscal mode (we use apic id), 1 means logical mode (we use set of processors).
	 * Bit 12: Delivery Status of the interrupt, read only. 0 = IDLE, 1 = Send Pending
	 * Bit 13: Not used
	 * Bit 14: Used for INIT mode aswell (1 = Assert, 0 = Deassert)
	 * Bit 15: Trigger Mode, read / write, 1 = Level sensitive, 0 = Edge sensitive.
	 * Bit 16-17: Reserved
	 * Bits 18-19: Destination Shorthand: 00 - Destination Field, 01 - Self, 10 - All, 11 - All others than self.
	 * Bits 56 - 63: Destination Field, if destination mode is physical, bits 56:59 should contain
	 *                                   an apic id. If it is logical, bits 56:63 defines a set of
	 *                                   processors that is the destination
	 */

	/* Setup flags
	 * Physical Destination
	 * Fixed Delivery
	 * Edge Sensitive
	 * Active High */
	IpiLow |= tVector;
	IpiLow |= (1 << 11);
	IpiLow |= (1 << 14);

	/* Setup Destination */
	IpiHigh |= ((ApicGetCpuMask(Core->Id) << 24));

	/* Wait for the ICR to clear
	 * before sending any vector */
	ApicWaitForIcr();

	/* Write upper 32 bits to ICR1 */
	ApicWriteLocal(APIC_ICR_HIGH, IpiHigh);

	/* Write lower 32 bits to ICR0
	 * this is the write that actually invokes
	 * the request, thats why we write to lower
	 * LAST */
	ApicWriteLocal(APIC_ICR_LOW, IpiLow);
}

/* Invoke an IPI request on either target
 * cpu, or on all cpu cores if a broadcast
 * has been requested. The supplied vector will
 * be the invoked interrupt */
void ApicSendIpi(UUId_t CpuTarget, uint32_t Vector)
{
	/* Broadcast or single request? */
	if (CpuTarget == 0xFF) {
		DataKey_t Key;
		Key.Value = ACPI_MADT_TYPE_LOCAL_APIC;
		ListExecuteOnKey(GlbAcpiNodes, ApicSendIpiLoop, Key, &Vector);
	}
	else
	{
		/* Variables for the IPI */
		uint32_t IpiLow = 0;
		uint32_t IpiHigh = 0;

		/* Structure of IO Entry Register:
		 * Bits 0 - 7: Interrupt Vector that will be raised (Valid ranges are from 0x10 - 0xFE) - Read/Write
		 * Bits 8 - 10: Delivery Mode. - Read / Write
		 *      - 000: Fixed Delivery, deliver interrupt to all cores listed in destination.
		 *      - 001: Lowest Priority, deliver interrupt to a core running lowest priority.
		 *      - 010: System Management Interrupt, must be edge triggered.
		 *      - 011: Reserved
		 *      - 100: NMI, deliver the interrupt to NMI signal of all cores, must be edge triggered.
		 *      - 101: INIT (Init Core)
		 *      - 110: SIPI (Start-Up)
		 *      - 111: ExtINT, Like fixed, requires edge triggered.
		 * Bit 11: Destination Mode, determines how the destination is interpreted. 0 means
		 *                           phyiscal mode (we use apic id), 1 means logical mode (we use set of processors).
		 * Bit 12: Delivery Status of the interrupt, read only. 0 = IDLE, 1 = Send Pending
		 * Bit 13: Not used
		 * Bit 14: Used for INIT mode aswell (1 = Assert, 0 = Deassert)
		 * Bit 15: Trigger Mode, read / write, 1 = Level sensitive, 0 = Edge sensitive.
		 * Bit 16-17: Reserved
		 * Bits 18-19: Destination Shorthand: 00 - Destination Field, 01 - Self, 10 - All, 11 - All others than self.
		 * Bits 56 - 63: Destination Field, if destination mode is physical, bits 56:59 should contain
		 *                                   an apic id. If it is logical, bits 56:63 defines a set of
		 *                                   processors that is the destination
		 */

		/* Setup flags
		 * Logical Destination
		 * Fixed Delivery
		 * Edge Sensitive */
		IpiLow |= Vector;
		IpiLow |= (1 << 11);
		IpiLow |= (1 << 14);

		/* Setup Destination */
		IpiHigh |= ((ApicGetCpuMask(CpuTarget) << 24));

		/* Wait for the ICR to clear 
		 * before sending any vector */
		ApicWaitForIcr();

		/* Write upper 32 bits to ICR1 */
		ApicWriteLocal(APIC_ICR_HIGH, IpiHigh);

		/* Write lower 32 bits to ICR0 
		 * this is the write that actually invokes
		 * the request, thats why we write to lower
		 * LAST */
		ApicWriteLocal(APIC_ICR_LOW, IpiLow);
	}
}
