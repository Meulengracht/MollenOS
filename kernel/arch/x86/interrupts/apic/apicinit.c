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
 *  - Initialization code for boot/ap cpus
 */

/* Includes 
 * - System */
#include <Apic.h>
#include <AcpiInterface.h>
#include <Memory.h>
#include <Interrupts.h>
#include <Timers.h>
#include <Heap.h>
#include <Log.h>

/* Includes
 * - C-Library */
#include <ds/list.h>
#include <string.h>

/* Globals 
 * We mark the GlbTimerTicks volatile
 * as it changes frequently */
size_t GlbTimerQuantum = APIC_DEFAULT_QUANTUM;
volatile size_t GlbTimerTicks[64];
List_t *GlbIoApics = NULL;
Addr_t GlbLocalApicBase = 0;
Cpu_t GlbBootstrapCpuId = 0;
int GlbIoApicI8259Pin = 0;
int GlbIoApicI8259Apic = 0;

/* Externs, we need access to some cpu information
 * and the ACPI nodes to get LVT information */
__CRT_EXTERN x86CpuObject_t GlbBootCpuInfo;
__CRT_EXTERN List_t *GlbAcpiNodes;

/* Handlers, they are defined in ApicHandlers.c
 * but are installed by the boot setup apic func */
__CRT_EXTERN InterruptStatus_t ApicErrorHandler(void *Args);
__CRT_EXTERN InterruptStatus_t ApicSpuriousHandler(void *Args);
__CRT_EXTERN InterruptStatus_t ApicTimerHandler(void *Args);

/* Setup LVT0/1 for the given cpu, it tries
 * to locate LVT information in the ACPI tables
 * and if not it defaults to sane values defined in
 * the APIC spec */
void ApicSetupLvt(Cpu_t Cpu, int Lvt)
{
	/* Variables for iteration */
	ListNode_t *Node;
	uint32_t Temp = 0;

	/* Iterate */
	_foreach(Node, GlbAcpiNodes) 
	{
		if (Node->Key.Value == ACPI_MADT_TYPE_LOCAL_APIC_NMI) 
		{
			/* Cast */
			ACPI_MADT_LOCAL_APIC_NMI *ApicNmi =
				(ACPI_MADT_LOCAL_APIC_NMI*)Node->Data;

			/* Is it for us? */
			if (ApicNmi->ProcessorId == 0xFF
				|| ApicNmi->ProcessorId == Cpu)
			{
				/* Yay, now we want to extract the settings
				 * given by ACPI and use them! */
				if (ApicNmi->Lint == Lvt) {
					Temp = APIC_NMI_ROUTE;
					Temp |= (InterruptGetPolarity(ApicNmi->IntiFlags, 0) << 13);
					Temp |= (InterruptGetTrigger(ApicNmi->IntiFlags, 0) << 15);
					break;
				}
			}
		}
	}

	/* Sanity - LVT 0 Default Settings
	 * Always set to EXTINT and level trigger */
	if (Temp == 0 && Lvt == 0) {
		Temp = APIC_EXTINT_ROUTE | APIC_LEVEL_TRIGGER;
	}

	/* Sanity - LVT 1 Default Settings
	 * They can be dependant on whether or not
	 * the apic is integrated or a seperated chip */
	if (Temp == 0 && Lvt == 1) {
		Temp = APIC_NMI_ROUTE;
		if (!ApicIsIntegrated()) 
			Temp |= APIC_LEVEL_TRIGGER;
	}

	/* Sanity, only BP gets LVT 
	 * so if this cpu doesn't equal, mask the interrupt */
	if (Cpu != GlbBootstrapCpuId) {
		Temp |= APIC_MASKED;
	}

	/* Write the LVT settings */
	if (Lvt == 1) {
		ApicWriteLocal(APIC_LINT1_REGISTER, Temp);
	}
	else {
		ApicWriteLocal(APIC_LINT0_REGISTER, Temp);
	}
}

/* This code initializes an io-apic, looks for the 
 * 8259 pin, clears out interrupts and makes sure
 * interrupts are masked */
void AcpiSetupIoApic(void *Data, int Nr, void *UserData)
{
	/* Cast Data */
	ACPI_MADT_IO_APIC *IoApic = 
		(ACPI_MADT_IO_APIC*)Data;
	DataKey_t Key;

	/* Sanitize the parameter, just in case */
	if (IoApic == NULL) {
		return;
	}

	/* Vars */
	int IoEntries, i, j;
	int IoApicNum = Nr;
	IoApic_t *IoListEntry = NULL;
	Addr_t RemapTo = (Addr_t)MmReserveMemory(1);

	/* Debug */
	LogInformation("APIC", "Initializing I/O Apic %u", IoApic->Id);

	/* Relocate the IoApic */
	MmVirtualMap(NULL, IoApic->Address, RemapTo, PAGE_CACHE_DISABLE);

	/* Allocate Entry */
	IoListEntry = (IoApic_t*)kmalloc(sizeof(IoApic_t));
	IoListEntry->GsiStart = IoApic->GlobalIrqBase;
	IoListEntry->Id = IoApic->Id;
	IoListEntry->BaseAddress = RemapTo + (IoApic->Address & 0xFFF);

	/* Maximum Redirection Entry—RO. This field contains the entry number (0 being the lowest
	 * entry) of the highest entry in the I/O Redirection Table. The value is equal to the number of
	 * interrupt input pins for the IOAPIC minus one. The range of values is 0 through 239. */
	IoEntries = ApicIoRead(IoListEntry, 1);
	IoEntries >>= 16;
	IoEntries &= 0xFF;

	/* Debug */
	LogInformation("APIC", "Io Entries: %u", IoEntries);

	/* Fill rest of info */
	IoListEntry->PinCount = IoEntries + 1;
	IoListEntry->Version = 0;

	/* Add to list */
	Key.Value = (int)IoApic->Id;
	ListAppend(GlbIoApics, ListCreateNode(Key, Key, IoListEntry));

	/* Structure of IO Entry Register:
	 * Bits 0 - 7: Interrupt Vector that will be raised (Valid ranges are from 0x10 - 0xFE) - Read/Write
	 * Bits 8 - 10: Delivery Mode. - Read / Write
	 *      - 000: Fixed Delivery, deliver interrupt to all cores listed in destination.
	 *      - 001: Lowest Priority, deliver interrupt to a core running lowest priority.
	 *      - 010: System Management Interrupt, must be edge triggered.
	 *      - 011: Reserved
	 *      - 100: NMI, deliver the interrupt to NMI signal of all cores, must be edge triggered.
	 *      - 101: INIT, deliver the signal to all cores by asserting init signal
	 *      - 110: Reserved
	 *      - 111: ExtINT, Like fixed, requires edge triggered.
	 * Bit 11: Destination Mode, determines how the destination is interpreted. 0 means
	 *                           phyiscal mode (we use apic id), 1 means logical mode (we use set of processors).
	 * Bit 12: Delivery Status of the interrupt, read only. 0 = IDLE, 1 = Send Pending
	 * Bit 13: Interrupt Pin Polarity, Read/Write, 0 = High active, 1 = Low active
	 * Bit 14: Remote IRR, read only. it is set to 0 when EOI has been recieved for that interrupt
	 * Bit 15: Trigger Mode, read / write, 1 = Level sensitive, 0 = Edge sensitive.
	 * Bit 16: Interrupt Mask, read / write, 1 = Masked, 0 = Unmasked.
	 * Bits 17 - 55: Reserved
	 * Bits 56 - 63: Destination Field, if destination mode is physical, bits 56:59 should contain
	 *                                   an apic id. If it is logical, bits 56:63 defines a set of
	 *                                   processors that is the destination
	 */

	/* Step 1 - find the i8259 connection */
	for (i = 0; i <= IoEntries; i++) {
		uint64_t Entry = ApicReadIoEntry(IoListEntry, i);

		/* Unmasked and ExtINT? 
		 * - Then we found it */ 
		if ((Entry & (APIC_MASKED | APIC_EXTINT_ROUTE)) == APIC_EXTINT_ROUTE) {
			GlbIoApicI8259Pin = i;
			GlbIoApicI8259Apic = IoApicNum;
			InterruptAllocateISA(i);
			break;
		}
	}

	/* Now clear interrupts */
	for (i = IoApic->GlobalIrqBase, j = 0; j <= IoEntries; i++, j++) {
		uint64_t Entry = ApicReadIoEntry(IoListEntry, j);

		/* Sanitize the entry
		 * We do NOT want to clear the SMI 
		 * and if it's an ISA we want to disable
		 * it for allocation */
		if (Entry & APIC_SMI_ROUTE) {
			if (j < 16) {
				InterruptAllocateISA(i);
			}
			continue;
		}

		/* Make sure entry is masked */
		if (!(Entry & APIC_MASKED)) {
			Entry |= APIC_MASKED;
			ApicWriteIoEntry(IoListEntry, j, Entry);
			Entry = ApicReadIoEntry(IoListEntry, j);
		}

		/* Check if Remote IRR is set 
		 * If it has been set we want to clear the 
		 * interrupt status for that irq line */
		if (Entry & 0x4000) {
			/* If it's not set to level trigger, we can't clear
			 * it, so modify it */
			if (!(Entry & APIC_LEVEL_TRIGGER)) {
				Entry |= APIC_LEVEL_TRIGGER;
				ApicWriteIoEntry(IoListEntry, j, Entry);
			}

			/* Send EOI */
			ApicSendEoi(j, (uint32_t)(Entry & 0xFF));
		}

		/* Mask it */
		ApicWriteIoEntry(IoListEntry, j, APIC_MASKED);
	}
}

/* Resets the local apic for the current
 * cpu and resets it to sane values, deasserts lines 
 * and clears errors */
void ApicClear(void)
{
	int MaxLvt = 0;
	uint32_t Temp = 0;

	/* Get Max LVT */
	MaxLvt = ApicGetMaxLvt();

	/* Mask error lvt */
	if (MaxLvt >= 3) {
		ApicWriteLocal(APIC_ERROR_REGISTER, INTERRUPT_LVTERROR | APIC_MASKED);
	}

	/* Mask these before deasserting */
	Temp = ApicReadLocal(APIC_TIMER_VECTOR);
	ApicWriteLocal(APIC_TIMER_VECTOR, Temp | APIC_MASKED);
	Temp = ApicReadLocal(APIC_LINT0_REGISTER);
	ApicWriteLocal(APIC_LINT0_REGISTER, Temp | APIC_MASKED);
	Temp = ApicReadLocal(APIC_LINT1_REGISTER);
	ApicWriteLocal(APIC_LINT1_REGISTER, Temp | APIC_MASKED);
	if (MaxLvt >= 4) {
		Temp = ApicReadLocal(APIC_PERF_MONITOR);
		ApicWriteLocal(APIC_PERF_MONITOR, Temp | APIC_MASKED);
	}

	/* Clean out APIC */
	ApicWriteLocal(APIC_TIMER_VECTOR, APIC_MASKED);
	ApicWriteLocal(APIC_LINT0_REGISTER, APIC_MASKED);
	ApicWriteLocal(APIC_LINT1_REGISTER, APIC_MASKED);
	if (MaxLvt >= 3) {
		ApicWriteLocal(APIC_ERROR_REGISTER, APIC_MASKED);
	}
	if (MaxLvt >= 4) {
		ApicWriteLocal(APIC_PERF_MONITOR, APIC_MASKED);
	}

	/* Integrated APIC (!82489DX) ? */
	if (ApicIsIntegrated()) {
		if (MaxLvt > 3) {
			/* Clear ESR due to Pentium errata 3AP and 11AP */
			ApicWriteLocal(APIC_ESR, 0);
		}
		ApicReadLocal(APIC_ESR);
	}
}

/* Basic initializationo of the local apic
 * chip, it resets the apic to a known default state
 * before we try and initialize */
void ApicInitialSetup(Cpu_t Cpu)
{
	/* Variables for init */
	uint32_t Temp = 0;
	int i = 0, j = 0;

	/* Clear Apic */
	ApicClear();

	/* Disable ESR */
#ifdef _X86_32
	if (ApicIsIntegrated()) {
		ApicWriteLocal(APIC_ESR, 0);
		ApicWriteLocal(APIC_ESR, 0);
		ApicWriteLocal(APIC_ESR, 0);
		ApicWriteLocal(APIC_ESR, 0);
	}
#endif

	/* Set perf monitor to NMI */
	ApicWriteLocal(APIC_PERF_MONITOR, APIC_NMI_ROUTE);

	/* Set destination format register to flat model */
	ApicWriteLocal(APIC_DEST_FORMAT, 0xFFFFFFFF);

	/* Set our cpu id */
	ApicWriteLocal(APIC_LOGICAL_DEST, (ApicGetCpuMask(Cpu) << 24));

	/* Set initial task priority to accept all */
	ApicSetTaskPriority(0);

	/* Clear interrupt registers ISR, IRR */
	for (i = 8 - 1; i >= 0; i--) {
		Temp = ApicReadLocal(0x100 + i * 0x10);
		for (j = 31; j >= 0; j--) {
			if (Temp & (1 << j)) {
				ApicSendEoi(0, 0);
			}
		}
	}
}

/* Initialization code for the local apic
 * ESR. It clears out the error registers and
 * the ESR register */
void ApicSetupESR(void)
{
	/* Variables for setup of ESR */
	int MaxLvt = 0;
	uint32_t Temp = 0;

	/* Sanitize whether or not this
	 * is an integrated chip, because
	 * if not ESR is not needed */
	if (!ApicIsIntegrated()) {
		return;
	}

	/* Get the max level of LVT supported
	 * on this local apic chip */
	MaxLvt = ApicGetMaxLvt();

	/* Sanitize - hammer down esr? */
	if (MaxLvt > 3) {
		ApicWriteLocal(APIC_ESR, 0);
	}
	Temp = ApicReadLocal(APIC_ESR);

	/* Enable sending errors */
	ApicWriteLocal(APIC_ERROR_REGISTER, INTERRUPT_LVTERROR);

	/* Clear errors after enabling */
	if (MaxLvt > 3) {
		ApicWriteLocal(APIC_ESR, 0);
	}
}

/* This code enables the local APIC
 * on the current processor calling
 * this code, should never be called more
 * than once! */
void ApicEnable(void)
{
	/* Variables */
	uint32_t Temp = 0;

	/* Enable local apic */
	Temp = ApicReadLocal(APIC_SPURIOUS_REG);
	Temp &= ~(0x000FF);
	Temp |= 0x100;

#ifdef _X86_32
	/* This reduces some problems with to fast
	 * interrupt mask/unmask */
	Temp &= ~(0x200);
#endif

	/* Set spurious vector */
	Temp |= INTERRUPT_SPURIOUS;

	/* Enable! */
	ApicWriteLocal(APIC_SPURIOUS_REG, Temp);
}

/* Reloads the local apic timer with a default
 * divisor and the timer set to the given quantum
 * the timer is immediately started */
void ApicReloadTimer(size_t Quantum)
{
	/* Setup timer settings */
	ApicWriteLocal(APIC_TIMER_VECTOR, APIC_TIMER_ONESHOT | INTERRUPT_LAPIC);
	ApicWriteLocal(APIC_DIVIDE_REGISTER, APIC_TIMER_DIVIDER_1);

	/* At-last, set the initial count, because
	 * it starts the counter immediately */
	ApicWriteLocal(APIC_INITIAL_COUNT, Quantum);
}

/* Initialize the local APIC controller
 * and install default interrupts. This
 * code also sets up the local APIC timer
 * with a default Quantum which is recalibrated
 * for accuracy once a timer is available */
void ApicInitBoot(void)
{
	/* Variables */
	ACPI_TABLE_HEADER *Header = NULL;
	MCoreInterrupt_t IrqInformation;
	Cpu_t BspApicId = 0;
	uint32_t Temp = 0;
	DataKey_t Key;

	/* Step 1. Disable IMCR if present (to-do..) 
	 * But the bit that tells us if IMCR is present
	 * is located in the MP tables */
	outb(0x22, 0x70);
	outb(0x23, 0x1);

	/* Clear out the global timer-tick counter
	 * we don't want any values in it previously :-) */
	memset((void*)GlbTimerTicks, 0, sizeof(GlbTimerTicks));

	/* Step 2. Get MADT and the LAPIC base 
	 * So we lookup the MADT table if it exists (if it doesn't
	 * we should fallback to MP tables, but not rn..) */
	if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_MADT, 0, &Header))) {
		ACPI_TABLE_MADT *MadtTable = (ACPI_TABLE_MADT*)Header;
		Addr_t RemapTo = (Addr_t)MmReserveMemory(1);

		/* We don't identity map it for several reasons, as we use
		 * higher space memory for stuff, so we allocate a new address
		 * for it! */
		LogInformation("APIC", "LAPIC address at 0x%x", MadtTable->Address);
		MmVirtualMap(NULL, MadtTable->Address, RemapTo, PAGE_CACHE_DISABLE);
		
		/* Now we can set it */
		GlbLocalApicBase = RemapTo + (MadtTable->Address & 0xFFF);
	}
	else {
		LogFatal("APIC", "Failed to get LAPIC base address, ABORT!!!");
		LogFatal("APIC", "Philip now we need MP-table support!");
		Idle();
	}

	/* Get the bootstrap processor id, and save it */
	BspApicId = (ApicReadLocal(APIC_PROCESSOR_ID) >> 24) & 0xFF;
	GlbBootstrapCpuId = BspApicId;

	/* Do some initial shared Apic setup
	 * for this processor id */
	ApicInitialSetup(BspApicId);

	/* Prepare some irq information */
	IrqInformation.Data = NULL;
	IrqInformation.Line = APIC_NO_GSI;
	IrqInformation.Pin = APIC_NO_GSI;

	/* Install Apic Handlers 
	 * - Spurious handlers
	 * - LVT Error handler
	 * - Timer handler */
	IrqInformation.Direct[0] = INTERRUPT_SPURIOUS7;
	IrqInformation.FastHandler = ApicSpuriousHandler;
	InterruptRegister(&IrqInformation, INTERRUPT_KERNEL | INTERRUPT_SOFTWARE);
	IrqInformation.Direct[0] = INTERRUPT_SPURIOUS;
	InterruptRegister(&IrqInformation, INTERRUPT_KERNEL | INTERRUPT_SOFTWARE);
	IrqInformation.Direct[0] = INTERRUPT_LVTERROR;
	IrqInformation.FastHandler = ApicErrorHandler;
	InterruptRegister(&IrqInformation, INTERRUPT_KERNEL | INTERRUPT_SOFTWARE);
	IrqInformation.Direct[0] = INTERRUPT_LAPIC;
	IrqInformation.FastHandler = ApicTimerHandler;
	InterruptRegister(&IrqInformation, INTERRUPT_KERNEL | INTERRUPT_SOFTWARE);

	/* Actually enable APIC on the
	 * boot processor, afterwards
	 * we do some more setup */
	ApicEnable();

	/* Setup LVT0 & LVT1 */
	ApicSetupLvt(BspApicId, 0);
	ApicSetupLvt(BspApicId, 1);

	/* Do the last shared setup code, which 
	 * sets up error registers */
	ApicSetupESR();

	/* Disable Apic Timer while we setup the io-apics 
	 * we need to be careful still */
	Temp = ApicReadLocal(APIC_TIMER_VECTOR);
	Temp |= (APIC_MASKED | INTERRUPT_LAPIC);
	ApicWriteLocal(APIC_TIMER_VECTOR, Temp);

	/* Setup IO apics 
	 * this is done by the AcpiSetupIoApic code
	 * that is called for all present io-apics */
	GlbIoApics = ListCreate(KeyInteger, LIST_NORMAL);
	Key.Value = ACPI_MADT_TYPE_IO_APIC;
	ListExecuteOnKey(GlbAcpiNodes, AcpiSetupIoApic, Key, NULL);

	/* We can now enable the interrupts, as 
	 * the IVT table is in place and the local apic
	 * has been configured! */
	LogInformation("APIC", "Enabling Interrupts");
	InterruptEnable();
	ApicSendEoi(0, 0);

	/* Start the timer to a defualt time-length */
	ApicReloadTimer(GlbTimerQuantum * 20);
}

/* Initialize the local APIC controller
 * on the ap cpu core. This 
 * code also sets up the local APIC timer
 * with a default Quantum */
void ApicInitAp(void)
{
	/* Variables for AP setup */
	Cpu_t ApicApId = 0;
	ApicApId = (ApicReadLocal(APIC_PROCESSOR_ID) >> 24) & 0xFF;

	/* Do some initial shared Apic setup
	 * for this processor id */
	ApicInitialSetup(ApicApId);

	/* Actually enable APIC on the
	 * ap processor, afterwards
	 * we do some more setup */
	ApicEnable();

	/* Setup LVT0 and LVT1 */
	ApicSetupLvt(ApicApId, 0);
	ApicSetupLvt(ApicApId, 1);

	/* Do the last shared setup code, which 
	 * sets up error registers */
	ApicSetupESR();

	/* Start the timer to a defualt time-length */
	ApicReloadTimer(GlbTimerQuantum * 20);
}

/* Recalibrates the the local apic 
 * timer, using an external timer source
 * this is to make the local apic timer more
 * accurate to make sure the quantum is 1 ms */
void ApicTimerRecalibrate(void)
{
	/* Variables for calibration */
	size_t TimerTicks = 0;

	/* Setup initial local apic timer registers */
	ApicWriteLocal(APIC_TIMER_VECTOR, INTERRUPT_LAPIC);
	ApicWriteLocal(APIC_DIVIDE_REGISTER, APIC_TIMER_DIVIDER_1);

	/* Start counters! */
	ApicWriteLocal(APIC_INITIAL_COUNT, 0xFFFFFFFF); /* Set counter to -1 */

	/* Stall, we have no other threads running! */
	StallMs(100);

	/* Stop counter! */
	ApicWriteLocal(APIC_TIMER_VECTOR, APIC_MASKED);

	/* Calculate bus frequency */
	TimerTicks = (0xFFFFFFFF - ApicReadLocal(APIC_CURRENT_COUNT));
	LogInformation("APIC", "Bus Speed: %u Hz", TimerTicks);
	GlbTimerQuantum = (TimerTicks / 100) + 1;

	/* We want a minimum of ca 400, this is to ensure on "slow"
	* computers we atleast get a few ms of processing power */
	if (GlbTimerQuantum < 400)
		GlbTimerQuantum = 400;

	LogInformation("APIC", "Quantum: %u", GlbTimerQuantum);

	/* Reset divider to make sure */
	ApicReloadTimer(GlbTimerQuantum * 20);
}
