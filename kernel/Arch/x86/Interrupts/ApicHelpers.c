/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS x86-32 Advanced Programmable Interrupt Controller Driver
* Helper Functions
*/

/* Includes */
#include <DeviceManager.h>
#include <Apic.h>
#include <acpi.h>
#include <Log.h>

/* C-Library */
#include <string.h>
#include <stdio.h>
#include <ds/list.h>

/* Externs */
extern List_t *GlbIoApics;
extern List_t *GlbAcpiNodes;
extern volatile uint32_t GlbTimerTicks[64];
extern volatile uint32_t GlbCpusBooted;
extern volatile Addr_t GlbLocalApicBase;
extern x86CpuObject_t GlbBootCpuInfo;

/* Get the version of the local apic */
uint32_t ApicGetVersion(void)
{
	return (ApicReadLocal(APIC_VERSION) & 0xFF);
}

/* Is the local apic integrated into the cpu? */
int ApicIsIntegrated(void)
{
	/* It always is on 64 bit */
#ifdef _X86_64
	return 1;
#else
	return (ApicGetVersion() & 0xF0);
#endif
}

int ApicIsModern(void)
{
	if (ApicGetVersion() >= 0x14)
		return 1;
	else
		return 0;
}

/* Get the max LVT */
int ApicGetMaxLvt(void)
{
	/* Read LVR */
	uint32_t Version = ApicReadLocal(APIC_VERSION);

	/* Calculate */
	return APIC_INTEGRATED((Version & 0xFF)) ? (((Version) >> 16) & 0xFF) : 2;
}

/* More helpers */
IoApic_t *ApicGetIoFromGsi(uint32_t Gsi)
{
	/* Convert Gsi to Pin & IoApic */
	foreach(i, GlbIoApics)
	{
		/* Cast */
		IoApic_t *Io = (IoApic_t*)i->Data;

		if (Io->GsiStart <= Gsi &&
			(Io->GsiStart + Io->PinCount) > Gsi)
			return Io;
	}

	/* Invalid Gsi */
	return NULL;
}

uint32_t ApicGetPinFromGsi(uint32_t Gsi)
{
	/* Convert Gsi to Pin & IoApic */
	foreach(i, GlbIoApics)
	{
		/* Cast */
		IoApic_t *Io = (IoApic_t*)i->Data;

		if (Io->GsiStart <= Gsi &&
			(Io->GsiStart + Io->PinCount) > Gsi)
			return Gsi - Io->GsiStart;
	}

	/* Damn? Wtf */
	return 0xFFFFFFFF;
}

/* Cpu Bitmask Functions */
uint32_t ApicGetCpuMask(uint32_t Cpu)
{
	/* Lets generate the bit */
	return 1 << Cpu;
}

/* Shortcut, set task priority register */
void ApicSetTaskPriority(uint32_t Priority)
{
	/* Write it to local apic */
	uint32_t Temp = ApicReadLocal(APIC_TASK_PRIORITY);
	Temp &= ~(APIC_PRIORITY_MASK);
	Temp |= (Priority & APIC_PRIORITY_MASK);
	ApicWriteLocal(APIC_TASK_PRIORITY, Priority);
}

uint32_t ApicGetTaskPriority(void)
{
	/* Write it to local apic */
	return (ApicReadLocal(APIC_TASK_PRIORITY) & APIC_PRIORITY_MASK);
}

/* Shortcut, send EOI */
void ApicSendEoi(uint32_t Gsi, uint32_t Vector)
{
	/* Lets see */
	if (ApicGetVersion() >= 0x10 || Gsi == 0xFFFFFFFF)
		ApicWriteLocal(APIC_INTERRUPT_ACK, Vector);
	else
	{
		/* Damn, it's the DX82 ! */
		uint64_t Entry, Entry1;
		IoApic_t *IoApic;
		uint32_t Pin;

		/* Get Io Apic */
		IoApic = ApicGetIoFromGsi(Gsi);
		Pin = ApicGetPinFromGsi(Gsi);

		if (IoApic == NULL || Pin == 0xFFFFFFFF)
		{
			LogFatal("APIC", "Invalid Gsi %u\n", Gsi);
			return;
		}

		/* Read Entry */
		Entry = Entry1 = ApicReadIoEntry(IoApic, 0x10 + (Pin * 2));

		/* Mask and set edge */
		Entry |= 0x10000;
		Entry &= ~(0x8000);

		/* Write */
		ApicWriteIoEntry(IoApic, Pin, Entry);

		/* Now restore it */
		ApicWriteIoEntry(IoApic, Pin, Entry1);

		/* Also */
		ApicWriteLocal(APIC_INTERRUPT_ACK, Vector);
	}
}

/* Wait for ICR to get idle */
void ApicWaitForIcr(void)
{
	while (ApicReadLocal(APIC_ICR_LOW) & APIC_ICR_BUSY)
		;
}

/* Send APIC Interrupt to a core */
void ApicSendIpiLoop(void *ApicInfo, int n, void *IrqVector)
{
	/* Cast */
	ACPI_MADT_LOCAL_APIC *Core = (ACPI_MADT_LOCAL_APIC*)ApicInfo;

	uint32_t tVector = *(uint32_t*)IrqVector;
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
	* */

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

	/* Wait */
	ApicWaitForIcr();

	/* Disable interrupts */
	IntStatus_t IntStatus = InterruptDisable();

	/* Write upper 32 bits to ICR1 */
	ApicWriteLocal(APIC_ICR_HIGH, IpiHigh);

	/* Write lower 32 bits to ICR0 */
	ApicWriteLocal(APIC_ICR_LOW, IpiLow);

	/* Enable */
	InterruptRestoreState(IntStatus);
}

void ApicSendIpi(uint8_t CpuTarget, uint8_t IrqVector)
{
	if (CpuTarget == 0xFF)
	{
		uint32_t tVector = (uint32_t)IrqVector;
		DataKey_t Key;

		/* Broadcast */
		Key.Value = ACPI_MADT_TYPE_LOCAL_APIC;
		ListExecuteOnKey(GlbAcpiNodes, ApicSendIpiLoop, Key, &tVector);
	}
	else
	{
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
		* */

		/* Setup flags
		* Logical Destination
		* Fixed Delivery
		* Edge Sensitive */
		IpiLow |= IrqVector;
		IpiLow |= (1 << 11);
		IpiLow |= (1 << 14);

		/* Setup Destination */
		IpiHigh |= ((ApicGetCpuMask(CpuTarget) << 24));

		/* Wait */
		ApicWaitForIcr();

		/* Disable interrupts */
		IntStatus_t IntStatus = InterruptDisable();

		/* Write upper 32 bits to ICR1 */
		ApicWriteLocal(APIC_ICR_HIGH, IpiHigh);

		/* Write lower 32 bits to ICR0 */
		ApicWriteLocal(APIC_ICR_LOW, IpiLow);

		/* Enable */
		InterruptRestoreState(IntStatus);
	}
}

/* Sync arb id's */
void ApicSyncArbIds(void)
{
	/* Not needed on AMD and not supported on P4 */

	/* Wait */
	ApicWaitForIcr();

	/* Sync Arb id's */
	ApicWriteLocal(APIC_ICR_HIGH, 0);
	ApicWriteLocal(APIC_ICR_LOW, 0x80000 | 0x8000 | 0x500);
	
	/* Wait */
	ApicWaitForIcr();
}

/* Get cpu id */
Cpu_t ApicGetCpu(void)
{
	/* Sanity */
	if (GlbLocalApicBase == 0)
		return 0;
	else
		return (ApicReadLocal(APIC_PROCESSOR_ID) >> 24) & 0xFF;
}

/* Debug Method -> Print ticks for current CPU */
void ApicPrintCpuTicks(void)
{
	int i;
	for (i = 0; i < (int)GlbCpusBooted; i++)
		LogDebug("APIC", "Cpu %u Ticks: %u\n", i, GlbTimerTicks[i]);
}