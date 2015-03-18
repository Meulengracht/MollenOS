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

#include <Arch.h>
#include <Acpi.h>
#include <LApic.h>
#include <Cpu.h>
#include <List.h>
#include <Thread.h>
#include <assert.h>
#include <stdio.h>

/* Drivers */
#include <SysTimers.h>

/* Globals */
volatile uint8_t GlbBootstrapCpuId = 0;
volatile uint32_t GlbTimerTicks[64];
volatile uint32_t GlbTimerQuantum = 0;

/* Externs */
extern volatile Addr_t local_apic_addr;
extern volatile uint32_t num_cpus;
extern volatile uint32_t glb_cpus_booted;
extern list_t *acpi_nodes;
extern void enter_thread(Registers_t *regs);

/* Primary CPU Timer IRQ */
int ApicTimerHandler(void *Args)
{
	/* Get registers */
	Registers_t *regs = NULL;
	uint32_t time_slice = 20;
	uint32_t task_priority = 0;
	Cpu_t cpu = ApicGetCpu();

	/* Uh oh */
	if (ApicReadLocal(LAPIC_CURRENT_COUNT) != 0
		|| ApicReadLocal(LAPIC_TIMER_VECTOR) & 0x10000)
		return X86_IRQ_NOT_HANDLED;

	/* Increase timer_ticks */
	GlbTimerTicks[cpu]++;

	/* Send EOI */
	ApicSendEoi();

	/* Switch Task */
	regs = threading_switch((Registers_t*)Args, 1, &time_slice, &task_priority);

	/* If we just got hold of idle task, well fuck it disable timer 
	 * untill we get another task */
	if (!(threading_get_current_thread(cpu)->Flags & X86_THREAD_IDLE))
	{
		/* Set Task Priority */
		ApicSetTaskPriority(61 - task_priority);

		/* Reset Timer Tick */
		ApicWriteLocal(LAPIC_INITIAL_COUNT, GlbTimerQuantum * time_slice);

		/* Re-enable timer in one-shot mode */
		ApicWriteLocal(LAPIC_TIMER_VECTOR, INTERRUPT_TIMER);		//0x20000 - Periodic
	}
	else
	{
		ApicWriteLocal(LAPIC_TIMER_VECTOR, 0x10000);
		ApicSetTaskPriority(0);
	}
	
	/* Enter new thread */
	enter_thread(regs);

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

/* This function redirects a IO Apic */
void ApicSetupIoApic(void *Data, int n)
{
	/* Cast Data */
	ACPI_MADT_IO_APIC *ioapic = (ACPI_MADT_IO_APIC*)Data;
	uint32_t io_entries, i, j;
	uint8_t io_apic_num = (uint8_t)n; 
	uint64_t io_entry_flags = 0;

	printf("    * Redirecting I/O Apic %u\n", ioapic->Id);

	/* Make sure address is mapped */
	if (!MmVirtualGetMapping(NULL, ioapic->Address))
		MmVirtualMap(NULL, ioapic->Address, ioapic->Address, 0);

	/* Maximum Redirection Entry—RO. This field contains the entry number (0 being the lowest
	 * entry) of the highest entry in the I/O Redirection Table. The value is equal to the number of
	 * interrupt input pins for the IOAPIC minus one. The range of values is 0 through 239. */
	io_entries = ApicIoRead(io_apic_num, 1);
	io_entries >>= 16;
	io_entries &= 0xFF;

	printf("    * IO Entries: %u\n", io_entries);

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

	/* Setup flags */
	io_entry_flags |= 0x0F;	/* Target all groups */
	io_entry_flags <<= 56;
	io_entry_flags |= 0x100; /* Lowest Priority */
	io_entry_flags |= 0x800; /* Logical Destination Mode */
	io_entry_flags |= 0x10000; /* Set masked */

	/* Map interrupts, registers start at 0x10 */
	/* Mask them all */
	for (i = ioapic->GlobalIrqBase, j = 0; j <= io_entries; i++, j++)
	{
		/* ex: we map io apic interrupts 0 - 23 to irq 32 - 55 */
		/* So when interrupts 0 - 23 gets raised, they point to 32 - 55 in IDT */
		ApicWriteIoEntry(io_apic_num, (0x10 + (j * 2)), (io_entry_flags | (0x20 + i)));
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

	/* Disable the PIC */
	outb(0x21, 0xFF);
	outb(0xA1, 0xFF);

	/* Force NMI and legacy through apic in IMCR */
	outb(0x22, 0x70);
	outb(0x23, 0x1);

	/* Setup Local Apic */
	ApicLocalInit();

	/* Now! Setup I/O Apics */
	list_execute_on_id(acpi_nodes, ApicSetupIoApic, ACPI_MADT_TYPE_IO_APIC);

	/* Install spurious handlers */
	InterruptInstallIdtOnly(INTERRUPT_SPURIOUS7, ApicSpuriousHandler, NULL);
	InterruptInstallIdtOnly(INTERRUPT_SPURIOUS, ApicSpuriousHandler, NULL);

	/* Done! Enable interrupts */
	printf("    * Enabling interrupts...\n");
	InterruptEnable();

	/* Kickstart things */
	ApicSendEoi();

	/* We install our timer here, we need it later on 
	 * TODO check for HPET first, then use RTC if it does not exist */
	printf("    * Installing Timer\n");
	timers_init();
}

/* Enable/Config LAPIC */
void ApicLocalInit(void)
{
	/* Vars */
	uint32_t bsp_apic = 0;

	/* Set destination format register to flat model */
	ApicWriteLocal(LAPIC_DEST_FORMAT, 0xF0000000);

	/* Set bits 31-24 for all cores to be in Group 1 in logical destination register */
	ApicWriteLocal(LAPIC_LOGICAL_DEST, 0xFF000000);

	/* Reset register states */
	ApicWriteLocal(LAPIC_ERROR_REGISTER, 0x10000);
	ApicWriteLocal(LAPIC_LINT0_REGISTER, 0x10000);
	ApicWriteLocal(LAPIC_LINT1_REGISTER, 0x10000);
	ApicWriteLocal(LAPIC_PERF_MONITOR, 0x10000);
	ApicWriteLocal(LAPIC_THERMAL_SENSOR, 0x10000);
	ApicWriteLocal(LAPIC_TIMER_VECTOR, 0x10000);

	/* Enable local apic */
	/* Install Spurious vector */
	ApicWriteLocal(LAPIC_SPURIOUS_REG, 0x100 | INTERRUPT_SPURIOUS);	// Set bit 8 in SVR.

	/* Set initial task priority */
	ApicSetTaskPriority(0);

	/* Get bootstrap CPU */
	bsp_apic = ApicReadLocal(0x20);
	bsp_apic >>= 24;
	GlbBootstrapCpuId = (uint8_t)(bsp_apic & 0xFF);
}

/* Enable/Config AP LAPIC */
void ApicApInit(void)
{
	/* Set destination format register to flat model */
	ApicWriteLocal(LAPIC_DEST_FORMAT, 0xF0000000);

	/* Set bits 31-24 for all cores to be in Group 1 in logical destination register */
	ApicWriteLocal(LAPIC_LOGICAL_DEST, 0xFF000000);

	/* Reset register states */
	ApicWriteLocal(LAPIC_ERROR_REGISTER, 0x10000);
	ApicWriteLocal(LAPIC_LINT0_REGISTER, 0x10000);
	ApicWriteLocal(LAPIC_LINT1_REGISTER, 0x10000);
	ApicWriteLocal(LAPIC_PERF_MONITOR, 0x10000);
	ApicWriteLocal(LAPIC_THERMAL_SENSOR, 0x10000);
	ApicWriteLocal(LAPIC_TIMER_VECTOR, 0x10000);

	/* Enable local apic */
	/* Install Spurious vector */
	ApicWriteLocal(LAPIC_SPURIOUS_REG, 0x100 | INTERRUPT_SPURIOUS);	// Set bit 8 in SVR.

	/* Set initial task priority */
	ApicSetTaskPriority(0);

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
	uint32_t divisor = 1193180 / 4;	/* 4 Hertz = 250 ms */
	uint8_t temp = 0;
	uint32_t lapic_ticks = 0;
	
	memset((void*)GlbTimerTicks, 0, sizeof(GlbTimerTicks));

	/* Setup initial local apic timer registers */
	ApicWriteLocal(LAPIC_TIMER_VECTOR, 0x2);
	ApicWriteLocal(LAPIC_DIVIDE_REGISTER, LAPIC_DIVIDER_1);

	/* Enable PIT Channel 2 */
	outb(0x63, (uint8_t)(inb(0x61) & 0xFD | 1));
	outb(0x43, 0xB2);

	outb(0x42, (uint8_t)(divisor & 0xFF));			/* Send divisor low byte */
	inb(0x60);										/* Short delay */
	outb(0x42, (uint8_t)((divisor >> 8) & 0xFF));	/* Send divisor high byte */

	temp = inb(0x61) & 0xFE;
	
	/* Start counters! */
	ApicWriteLocal(LAPIC_INITIAL_COUNT, 0xFFFFFFFF); /* Set counter to -1 */

	outb(0x61, temp);								//gate low
	outb(0x61, temp | 1);							//gate high

	/* Wait for PIT to reach zero */
	while (!(inb(0x61) & 0x20));

	/* Stop counter! */
	ApicWriteLocal(LAPIC_TIMER_VECTOR, 0x10000);

	/* Calculate bus frequency */
	lapic_ticks = (0xFFFFFFFF - ApicReadLocal(LAPIC_CURRENT_COUNT));
	printf("    * Ticks: %u\n", lapic_ticks);
	GlbTimerQuantum = ((lapic_ticks * 4) / 1000) + 1;

	/* We want a minimum of ca 400, this is to ensure on "slow"
	 * computers we atleast get a few ms of processing power */
	if (GlbTimerQuantum < 400)
		GlbTimerQuantum = 400;

	printf("    * Quantum: %u\n", GlbTimerQuantum);

	/* Install Interrupt */
	InterruptInstallBroadcast(0, INTERRUPT_TIMER, ApicTimerHandler, NULL);

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
	ApicWriteLocal(0x80, Priority);
}

uint32_t ApicGetTaskPriority(void)
{
	/* Write it to local apic */
	return (ApicReadLocal(0x80) & 0xFF);
}

/* Shortcut, send EOI */
void ApicSendEoi(void)
{
	/* Dummy read for dodgy pentium cpus */
	ApicReadLocal(0x30);

	/* Send EOI to local apic */
	ApicWriteLocal(LAPIC_INTERRUPT_ACK, 0);
}

/* Read / Write to io apic registers */
void ApicSetIoRegister(ACPI_MADT_IO_APIC *io, uint32_t Register)
{
	/* Set register */
	(*(volatile Addr_t*)(io->Address)) = (uint32_t)Register;
}

/* Read from Io Apic register */
uint32_t ApicIoRead(uint8_t IoApic, uint32_t Register)
{
	/* Get the correct IO APIC address */
	ACPI_MADT_IO_APIC *io =
		(ACPI_MADT_IO_APIC*)list_get_data_by_id(acpi_nodes, ACPI_MADT_TYPE_IO_APIC, IoApic);

	/* Sanity */
	assert(io != NULL);

	/* Select register */
	ApicSetIoRegister(io, Register);

	/* Read */
	return *(volatile Addr_t*)(io->Address + 0x10);
}

/* Write to Io Apic register */
void ApicIoWrite(uint8_t IoApic, uint32_t Register, uint32_t Data)
{
	/* Get the correct IO APIC address */
	ACPI_MADT_IO_APIC *io =
		(ACPI_MADT_IO_APIC*)list_get_data_by_id(acpi_nodes, ACPI_MADT_TYPE_IO_APIC, IoApic);

	/* Sanity */
	assert(io != NULL);

	/* Select register */
	ApicSetIoRegister(io, Register);

	/* Write */
	*(volatile Addr_t*)(io->Address + 0x10) = Data;
}

/* Insert a 64 bit entry into the Io Apic Intr */
void ApicWriteIoEntry(uint8_t IoApic, uint32_t Register, uint64_t Data)
{
	/* Get the correct IO APIC address */
	ACPI_MADT_IO_APIC *io =
		(ACPI_MADT_IO_APIC*)list_get_data_by_id(acpi_nodes, ACPI_MADT_TYPE_IO_APIC, IoApic);

	/* Sanity */
	assert(io != NULL);

	/* Select register */
	ApicSetIoRegister(io, Register + 1);

	/* Write lower data */
	(*(volatile Addr_t*)(io->Address + 0x10)) = (uint32_t)(Data >> 32);
	
	/* Reselect, write upper */
	ApicSetIoRegister(io, Register);
	(*(volatile Addr_t*)(io->Address + 0x10)) = (uint32_t)(Data & 0xFFFFFFFF);
}

/* Retrieve a 64 bit entry from the Io Apic Intr */
uint64_t ApicReadIoEntry(uint8_t IoApic, uint32_t Register)
{
	uint32_t lo, hi;
	uint64_t val;

	/* Get the correct IO APIC address */
	ACPI_MADT_IO_APIC *io =
		(ACPI_MADT_IO_APIC*)list_get_data_by_id(acpi_nodes, ACPI_MADT_TYPE_IO_APIC, IoApic);

	/* Sanity */
	assert(io != NULL);

	/* Select register */
	ApicSetIoRegister(io, Register);

	/* Read high word */
	hi = *(volatile Addr_t*)(io->Address + 0x10);

	/* Select next register */
	ApicSetIoRegister(io, Register + 1);

	/* Read low word */
	lo = *(volatile Addr_t*)(io->Address + 0x10);

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
	for (i = 0; i < (int)glb_cpus_booted; i++)
		printf("Cpu %u Ticks: %u\n", i, GlbTimerTicks[i]);
}