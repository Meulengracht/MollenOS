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
*/

/* Includes */
#include <Arch.h>
#include <Acpi.h>
#include <LApic.h>
#include <Cpu.h>
#include <List.h>
#include <Thread.h>
#include <assert.h>
#include <stdio.h>
#include <Heap.h>

/* Drivers */
#include <SysTimers.h>

/* Globals */
volatile uint8_t GlbBootstrapCpuId = 0;
volatile uint32_t GlbTimerTicks[64];
volatile uint32_t GlbTimerQuantum = 0;
list_t *GlbIoApics = NULL;
uint32_t GlbIoApicI8259Pin = 0;
uint32_t GlbIoApicI8259Apic = 0;

/* Externs */
extern volatile Addr_t local_apic_addr;
extern volatile uint32_t num_cpus;
extern volatile uint32_t GlbCpusBooted;
extern volatile uint32_t GlbNumIoApics;
extern CpuObject_t boot_cpu_info;
extern list_t *acpi_nodes;
extern void enter_thread(Registers_t *regs);

/* Helpers */
uint32_t ApicGetVersion(void)
{
	return (ApicReadLocal(LAPIC_VERSION) & 0xFF);
}

int ApicLocalIsIntegrated(void)
{
#ifdef _X86_64
	return 1;
#else
	return (ApicGetVersion() & 0xF0);
#endif
}

int ApicGetMaxLvt(void)
{
	/* Read LVR */
	uint32_t Version = ApicReadLocal(LAPIC_VERSION);

	/* Calculate */
	return APIC_INTEGRATED((Version & 0xFF)) ? (((Version) >> 16) & 0xFF) : 2;
}

/* More helpers */
IoApic_t *ApicGetIoFromGsi(uint32_t Gsi)
{
	list_node_t *i;

	/* Convert Gsi to Pin & IoApic */
	_foreach(i, GlbIoApics)
	{
		/* Cast */
		IoApic_t *Io = (IoApic_t*)i->data;

		if (Io->GsiStart <= Gsi &&
			(Io->GsiStart + Io->PinCount) > Gsi)
			return Io;
	}

	/* Invalid Gsi */
	return NULL;
}

uint32_t ApicGetPinFromGsi(uint32_t Gsi)
{
	list_node_t *i;

	/* Convert Gsi to Pin & IoApic */
	_foreach(i, GlbIoApics)
	{
		/* Cast */
		IoApic_t *Io = (IoApic_t*)i->data;

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

/* Primary CPU Timer IRQ */
int ApicTimerHandler(void *Args)
{
	/* Get registers */
	Registers_t *Regs = NULL;
	uint32_t TimeSlice = 20;
	uint32_t TaskPriority = 0;
	Cpu_t CurrCpu = ApicGetCpu();

	/* Uh oh */
	if (ApicReadLocal(LAPIC_CURRENT_COUNT) != 0
		|| ApicReadLocal(LAPIC_TIMER_VECTOR) & 0x10000)
		return X86_IRQ_NOT_HANDLED;

	/* Increase timer_ticks */
	GlbTimerTicks[CurrCpu]++;

	/* Send EOI */
	ApicSendEoi(0, INTERRUPT_TIMER);

	/* Switch Task */
	Regs = ThreadingSwitch((Registers_t*)Args, 1, &TimeSlice, &TaskPriority);

	/* If we just got hold of idle task, well fuck it disable timer 
	 * untill we get another task */
	if (!(ThreadingGetCurrentThread(CurrCpu)->Flags & X86_THREAD_IDLE))
	{
		/* Set Task Priority */
		ApicSetTaskPriority(61 - TaskPriority);

		/* Reset Timer Tick */
		ApicWriteLocal(LAPIC_INITIAL_COUNT, GlbTimerQuantum * TimeSlice);

		/* Re-enable timer in one-shot mode */
		ApicWriteLocal(LAPIC_TIMER_VECTOR, INTERRUPT_TIMER);		//0x20000 - Periodic
	}
	else
	{
		ApicWriteLocal(LAPIC_TIMER_VECTOR, 0x10000);
		ApicSetTaskPriority(0);
	}
	
	/* Enter new thread */
	enter_thread(Regs);

	/* Never reached */
	return X86_IRQ_HANDLED;
}

/* Spurious handler */
int ApicSpuriousHandler(void *Args)
{
	/* Unused */
	_CRT_UNUSED(Args);

	/* Yay, we handled it ... */
	return X86_IRQ_HANDLED;
}

/* Error Handler */
int ApicErrorHandler(void *Args)
{
	/* Unused */
	_CRT_UNUSED(Args);

	/* Yay, we handled it ... */
	return X86_IRQ_HANDLED;
}

/* This function redirects a IO Apic */
void ApicSetupIoApic(void *Data, int n)
{
	/* Cast Data */
	ACPI_MADT_IO_APIC *ioapic = (ACPI_MADT_IO_APIC*)Data;
	uint32_t io_entries, i, j;
	uint8_t io_apic_num = (uint8_t)n;
	IoApic_t *IoListEntry = NULL;

	printf("    * Redirecting I/O Apic %u\n", ioapic->Id);

	/* Make sure address is mapped */
	if (!MmVirtualGetMapping(NULL, ioapic->Address))
		MmVirtualMap(NULL, ioapic->Address, ioapic->Address, 0);

	/* Allocate Entry */
	IoListEntry = (IoApic_t*)kmalloc(sizeof(IoApic_t));
	IoListEntry->GsiStart = ioapic->GlobalIrqBase;
	IoListEntry->Id = ioapic->Id;
	IoListEntry->BaseAddress = ioapic->Address;

	/* Maximum Redirection Entry—RO. This field contains the entry number (0 being the lowest
	 * entry) of the highest entry in the I/O Redirection Table. The value is equal to the number of
	 * interrupt input pins for the IOAPIC minus one. The range of values is 0 through 239. */
	io_entries = ApicIoRead(IoListEntry, 1);
	io_entries >>= 16;
	io_entries &= 0xFF;

	printf("    * IO Entries: %u\n", io_entries);

	/* Fill rest of info */
	IoListEntry->PinCount = io_entries + 1;
	IoListEntry->Version = 0;

	/* Add to list */
	list_append(GlbIoApics, list_create_node(ioapic->Id, IoListEntry));

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
	 * */

	/* Step 1 - find the i8259 connection */
	for (i = 0; i <= io_entries; i++)
	{
		/* Read Entry */
		uint64_t Entry = ApicReadIoEntry(IoListEntry, 0x10 + (2 * i));

		/* Unmasked and ExtINT? */
		if ((Entry & 0x10700) == 0x700)
		{
			/* We found it */
			GlbIoApicI8259Pin = i;
			GlbIoApicI8259Apic = (uint32_t)io_apic_num;

			InterruptAllocateISA(i);
			break;
		}
	}

	/* Now clear interrupts */
	for (i = ioapic->GlobalIrqBase, j = 0; j <= io_entries; i++, j++)
	{
		/* Do not clear SMI! */
		uint64_t Entry = ApicReadIoEntry(IoListEntry, 0x10 + (2 * j));

		/* Sanity */
		if (Entry & 0x200)
		{
			/* Disable this interrupt for our usage */
			if (j < 16)
				InterruptAllocateISA(i);
			continue;
		}
			
		/* Make sure entry is masked */
		if (!(Entry & 0x10000))
		{
			Entry |= 0x10000;
			ApicWriteIoEntry(IoListEntry, j, Entry);
			Entry = ApicReadIoEntry(IoListEntry, 0x10 + (2 * j));
		}

		/* Check if Remote IRR is set */
		if (Entry & 0x4000)
		{
			/* Make sure it is set to level, otherwise we cannot clear it */
			if (!(Entry & 0x8000))
			{
				Entry |= 0x8000;
				ApicWriteIoEntry(IoListEntry, j, Entry);
			}

			/* Send EOI */
			ApicSendEoi(j, (uint32_t)(Entry & 0xFF));
		}

		/* Mask it */
		ApicWriteIoEntry(IoListEntry, j, 0x10000);
	}
}

void ApicBspInit(void)
{
	/* Inititalize & Remap PIC. WE HAVE TO DO THIS :( */
	
	/* Send INIT (0x10) and IC4 (0x1) Commands*/
	outb(0x20, 0x11);
	outb(0xA0, 0x11);

	/* Remap primary PIC to 0x20 - 0x28 */
	outb(0x21, 0x20);

	/* Remap Secondary PIC to 0x28 - 0x30 */
	outb(0xA1, 0x28);

	/* Send initialization words, they define 
	 * which PIC connects to where */
	outb(0x21, 0x04);
	outb(0xA1, 0x02);

	/* Enable i86 mode */
	outb(0x21, 0x01);
	outb(0xA1, 0x01);

	/* Mask all irqs in PIC */
	outb(0x21, 0xFF);
	outb(0xA1, 0xFF);

	/* Use PIC? */
	if (GlbNumIoApics != 0)
	{
		/* Force NMI and legacy through apic in IMCR */
		outb(0x22, 0x70);
		outb(0x23, 0x1);
	}

	/* Setup Local Apic */
	ApicLocalInit();

	/* Now! Setup I/O Apics */
	if (GlbNumIoApics != 0)
	{
		GlbIoApics = list_create(LIST_NORMAL);
		list_execute_on_id(acpi_nodes, ApicSetupIoApic, ACPI_MADT_TYPE_IO_APIC);
	}	
}

/* Enable/Config LAPIC */
void ApicLocalInit(void)
{
	/* Vars */
	uint32_t bsp_apic = 0;
	uint32_t Temp = 0;
	int i = 0, j = 0;

	/* Get Apic Id */
	bsp_apic = (ApicReadLocal(LAPIC_PROCESSOR_ID) >> 24) & 0xFF;

	/* Disable ESR */
#ifdef _X86_32
	if (ApicLocalIsIntegrated())
	{
		ApicWriteLocal(LAPIC_ESR, 0);
		ApicWriteLocal(LAPIC_ESR, 0);
		ApicWriteLocal(LAPIC_ESR, 0);
		ApicWriteLocal(LAPIC_ESR, 0);
	}
#endif

	/* Set perf monitor to NMI */
	ApicWriteLocal(LAPIC_PERF_MONITOR, 0x400);

	/* Set destination format register to flat model */
	ApicWriteLocal(LAPIC_DEST_FORMAT, 0xFFFFFFFF);

	/* Set our cpu id */
	ApicWriteLocal(LAPIC_LOGICAL_DEST, (ApicGetCpuMask(bsp_apic) << 24));

	/* Clear interrupt registers ISR, IRR */
	for (i = 8 - 1; i >= 0; i--) 
	{
		Temp = ApicReadLocal(0x100 + i * 0x10);
		for (j = 31; j >= 0; j--) 
		{
			if (Temp & (1 << j))
				ApicSendEoi(0, 0);
		}
	}

	/* Set initial task priority to accept all */
	ApicSetTaskPriority(0);

	/* Install Apic Handlers */
	InterruptInstallIdtOnly(0xFFFFFFFF, INTERRUPT_SPURIOUS7, ApicSpuriousHandler, NULL);
	InterruptInstallIdtOnly(0xFFFFFFFF, INTERRUPT_SPURIOUS, ApicSpuriousHandler, NULL);

	/* Enable local apic */
	Temp = ApicReadLocal(LAPIC_SPURIOUS_REG);
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
	ApicWriteLocal(LAPIC_SPURIOUS_REG, Temp);

	/* Setup LVT0 to Virtual Wire Mode */
	Temp = ApicReadLocal(LAPIC_LINT0_REGISTER) & 0x10000;
	if (Temp == 0 || GlbNumIoApics == 0)
		Temp |= 0x700;
	else
		Temp |= (0x10000 | 0x700);
	ApicWriteLocal(LAPIC_LINT0_REGISTER, Temp);

	/* Now LVT1 ONLY IF ACPI tells us about it */
	Temp = 0x400;
	if (!ApicLocalIsIntegrated()) /* Set level triggered */
		Temp |= 0x8000;

	ApicWriteLocal(LAPIC_LINT1_REGISTER, Temp);

	/* Get bootstrap CPU */
	GlbBootstrapCpuId = (uint8_t)bsp_apic;
}

/* Finish LAPIC Config */
void ApicLocalFinish(void)
{
	/* Vars */
	int MaxLvt = 0;
	uint32_t Temp = 0;

	/* Sanity */
	if (!ApicLocalIsIntegrated())
		goto Skip;

	/* Setup ESR */
	MaxLvt = ApicGetMaxLvt();

	/* Sanity */
	if (MaxLvt > 3)
		ApicWriteLocal(LAPIC_ESR, 0);
	Temp = ApicReadLocal(LAPIC_ESR);

	/* Install Interrupt Vector */
	InterruptInstallIdtOnly(0xFFFFFFFF, INTERRUPT_LVTERROR, ApicErrorHandler, NULL);

	/* Enable sending errors */
	ApicWriteLocal(LAPIC_ERROR_REGISTER, INTERRUPT_LVTERROR);

	/* Clear errors after enabling */
	if (MaxLvt > 3)
		ApicWriteLocal(LAPIC_ESR, 0);

#ifdef _X86_32
	/* Disable Apic Timer */
	Temp = ApicReadLocal(LAPIC_TIMER_VECTOR);
	Temp |= (0x10000 | INTERRUPT_TIMER);
	ApicWriteLocal(LAPIC_TIMER_VECTOR, Temp);
#endif

Skip:
	/* Done! Enable interrupts */
	printf("    * Enabling interrupts...\n");
	InterruptEnable();

	/* Kickstart things */
	ApicSendEoi(0, 0);

	/* Setup Timers */
	printf("    * Installing Timers...\n");
	TimerManagerInit();
}

/* Enable/Config AP LAPIC */
void ApicApInit(void)
{
	/* Vars */
	uint32_t Temp = 0;
	uint32_t ap_apic = 0;
	int i = 0, j = 0;
	ap_apic = (ApicReadLocal(LAPIC_PROCESSOR_ID) >> 24) & 0xFF;

	/* Disable ESR */
#ifdef _X86_32
	if (ApicLocalIsIntegrated())
	{
		ApicWriteLocal(LAPIC_ESR, 0);
		ApicWriteLocal(LAPIC_ESR, 0);
		ApicWriteLocal(LAPIC_ESR, 0);
		ApicWriteLocal(LAPIC_ESR, 0);
	}
#endif

	/* Set perf monitor to NMI */
	ApicWriteLocal(LAPIC_PERF_MONITOR, 0x400);

	/* Set destination format register to flat model */
	ApicWriteLocal(LAPIC_DEST_FORMAT, 0xFFFFFFFF);

	/* Set our cpu id */
	ApicWriteLocal(LAPIC_LOGICAL_DEST, (ApicGetCpuMask(ap_apic) << 24));

	/* Set initial task priority */
	ApicSetTaskPriority(0);

	/* Clear interrupt registers ISR, IRR */
	for (i = 8 - 1; i >= 0; i--)
	{
		Temp = ApicReadLocal(0x100 + i * 0x10);
		for (j = 31; j >= 0; j--)
		{
			if (Temp & (1 << j))
				ApicSendEoi(0, 0);
		}
	}

	/* Enable local apic */
	Temp = ApicReadLocal(LAPIC_SPURIOUS_REG);
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
	ApicWriteLocal(LAPIC_SPURIOUS_REG, Temp);

	/* Setup LVT0 and LVT1 */
	Temp = ApicReadLocal(LAPIC_LINT0_REGISTER) & 0x10000;
	Temp |= (0x10000 | 0x700);
	ApicWriteLocal(LAPIC_LINT0_REGISTER, Temp);

	/* Now LVT1 */
	Temp = 0x400 | 0x10000;
	if (!ApicLocalIsIntegrated()) /* Set level triggered */
		Temp |= 0x8000;

	ApicWriteLocal(LAPIC_LINT1_REGISTER, Temp);

	/* Set divider */
	ApicWriteLocal(LAPIC_DIVIDE_REGISTER, LAPIC_DIVIDER_1);

	/* Setup timer */
	ApicWriteLocal(LAPIC_INITIAL_COUNT, GlbTimerQuantum * 20);

	/* Enable timer in one-shot mode */
	ApicWriteLocal(LAPIC_TIMER_VECTOR, INTERRUPT_TIMER);		//0x20000 - Periodic
}

/* Send APIC Interrupt to a core */
void ApicSendIpi(uint8_t CpuTarget, uint8_t IrqVector)
{
	if (CpuTarget == 0xFF)
	{
		/* Broadcast */
	}
	else
	{
		uint64_t int_setup = 0;

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
		int_setup = CpuTarget;
		int_setup <<= 56;
		int_setup |= (1 << 14);
		int_setup |= IrqVector;

		/* Write upper 32 bits to ICR1 */
		ApicWriteLocal(0x310, (uint32_t)((int_setup >> 32) & 0xFFFFFFFF));

		/* Write lower 32 bits to ICR0 */
		ApicWriteLocal(0x300, (uint32_t)(int_setup & 0xFFFFFFFF));
	}
}

/* Enable PIT & Local Apic */
void ApicTimerInit(void)
{
	/* Vars */
	uint32_t TimerTicks = 0;
	
	/* Zero out */
	memset((void*)GlbTimerTicks, 0, sizeof(GlbTimerTicks));

	/* Install Interrupt */
	InterruptInstallIdtOnly(0xFFFFFFFF, INTERRUPT_TIMER, ApicTimerHandler, NULL);

	/* Setup initial local apic timer registers */
	ApicWriteLocal(LAPIC_TIMER_VECTOR, INTERRUPT_TIMER);
	ApicWriteLocal(LAPIC_DIVIDE_REGISTER, LAPIC_DIVIDER_1);

	/* Repeat */
	ApicWriteLocal(LAPIC_LINT0_REGISTER, 0x8000 | 0x700);
	ApicWriteLocal(LAPIC_LINT1_REGISTER, 0x400);
	ApicWriteLocal(LAPIC_PERF_MONITOR, 0x400);

	/* Start counters! */
	ApicWriteLocal(LAPIC_INITIAL_COUNT, 0xFFFFFFFF); /* Set counter to -1 */

	/* Stall, we have no other threads running! */
	StallMs(100);

	/* Stop counter! */
	ApicWriteLocal(LAPIC_TIMER_VECTOR, 0x10000);

	/* Calculate bus frequency */
	TimerTicks = (0xFFFFFFFF - ApicReadLocal(LAPIC_CURRENT_COUNT));
	printf("    * Ticks: %u\n", TimerTicks);
	GlbTimerQuantum = (TimerTicks / 100) + 1;

	/* We want a minimum of ca 400, this is to ensure on "slow"
	 * computers we atleast get a few ms of processing power */
	if (GlbTimerQuantum < 400)
		GlbTimerQuantum = 400;

	printf("    * Quantum: %u\n", GlbTimerQuantum);

	/* Reset Timer Tick */
	ApicWriteLocal(LAPIC_INITIAL_COUNT, GlbTimerQuantum * 20);

	/* Re-enable timer in one-shot mode */
	ApicWriteLocal(LAPIC_TIMER_VECTOR, INTERRUPT_TIMER);		//0x20000 - Periodic

	/* Reset divider to make sure */
	ApicWriteLocal(LAPIC_DIVIDE_REGISTER, LAPIC_DIVIDER_1);
}

/* Read / Write to local apic registers */
uint32_t ApicReadLocal(uint32_t Register)
{
	return (uint32_t)(*(volatile Addr_t*)(local_apic_addr + Register));
}

void ApicWriteLocal(uint32_t Register, uint32_t Value)
{
	(*(volatile Addr_t*)(local_apic_addr + Register)) = Value;
}

/* Shortcut, set task priority register */
void ApicSetTaskPriority(uint32_t Priority)
{
	/* Write it to local apic */
	uint32_t Temp = ApicReadLocal(LAPIC_TASK_PRIORITY);
	Temp &= ~(LAPIC_PRIORITY_MASK);
	Temp |= (Priority & LAPIC_PRIORITY_MASK);
	ApicWriteLocal(LAPIC_TASK_PRIORITY, Priority);
}

uint32_t ApicGetTaskPriority(void)
{
	/* Write it to local apic */
	return (ApicReadLocal(LAPIC_TASK_PRIORITY) & LAPIC_PRIORITY_MASK);
}

/* Shortcut, send EOI */
void ApicSendEoi(uint32_t Gsi, uint32_t Vector)
{
	/* Lets see */
	if (ApicGetVersion() >= 0x10 || Gsi == 0xFFFFFFFF)
		ApicWriteLocal(LAPIC_INTERRUPT_ACK, Vector);
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
			printf("Invalid Gsi %u\n", Gsi);
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
	}
}

/* Read / Write to io apic registers */
void ApicSetIoRegister(IoApic_t *IoApic, uint32_t Register)
{
	/* Set register */
	(*(volatile Addr_t*)(IoApic->BaseAddress)) = (uint32_t)Register;
}

/* Read from Io Apic register */
uint32_t ApicIoRead(IoApic_t *IoApic, uint32_t Register)
{
	/* Sanity */
	assert(IoApic != NULL);

	/* Select register */
	ApicSetIoRegister(IoApic, Register);

	/* Read */
	return *(volatile Addr_t*)(IoApic->BaseAddress + 0x10);
}

/* Write to Io Apic register */
void ApicIoWrite(IoApic_t *IoApic, uint32_t Register, uint32_t Data)
{
	/* Sanity */
	assert(IoApic != NULL);

	/* Select register */
	ApicSetIoRegister(IoApic, Register);

	/* Write */
	*(volatile Addr_t*)(IoApic->BaseAddress + 0x10) = Data;
}

/* Insert a 64 bit entry into the Io Apic Intr */
void ApicWriteIoEntry(IoApic_t *IoApic, uint32_t Pin, uint64_t Data)
{
	/* Get the correct IO APIC address */
	uint32_t IoReg = 0x10 + (2 * Pin);

	/* Sanity */
	assert(IoApic != NULL);

	/* Select register */
	ApicSetIoRegister(IoApic, IoReg + 1);

	/* Write High data */
	(*(volatile Addr_t*)(IoApic->BaseAddress + 0x10)) = ACPI_HIDWORD(Data);

	/* Reselect*/
	ApicSetIoRegister(IoApic, IoReg);

	/* Write lower */
	(*(volatile Addr_t*)(IoApic->BaseAddress + 0x10)) = ACPI_LODWORD(Data);
}

/* Retrieve a 64 bit entry from the Io Apic Intr */
uint64_t ApicReadIoEntry(IoApic_t *IoApic, uint32_t Register)
{
	uint32_t lo, hi;
	uint64_t val;

	/* Sanity */
	assert(IoApic != NULL);

	/* Select register */
	ApicSetIoRegister(IoApic, Register + 1);

	/* Read high word */
	hi = *(volatile Addr_t*)(IoApic->BaseAddress + 0x10);

	/* Select next register */
	ApicSetIoRegister(IoApic, Register);

	/* Read low word */
	lo = *(volatile Addr_t*)(IoApic->BaseAddress + 0x10);

	/* Build */
	val = hi;
	val <<= 32;
	val |= lo;
	return val;
}

/* Get cpu id */
Cpu_t ApicGetCpu(void)
{
	/* Sanity */
	if (num_cpus <= 1)
		return 0;
	else
		return (ApicReadLocal(LAPIC_PROCESSOR_ID) >> 24) & 0xFF;
}

/* Debug Method -> Print ticks for current CPU */
void ApicPrintCpuTicks(void)
{
	int i;
	for (i = 0; i < (int)GlbCpusBooted; i++)
		printf("Cpu %u Ticks: %u\n", i, GlbTimerTicks[i]);
}