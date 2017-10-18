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
 *  - Helper functions and utility functions
 */

/* Includes 
 * - System */
#include <apic.h>
#include <acpi.h>
#include <log.h>

/* Includes
 * - C-Library */
#include <string.h>
#include <stdio.h>
#include <ds/list.h>

/* Externs, we need access to a lot of different
 * things for these helper functions */
__EXTERN volatile size_t GlbTimerTicks[64];
__EXTERN volatile int GlbCpusBooted;
__EXTERN uintptr_t GlbLocalApicBase;
__EXTERN List_t *GlbIoApics;
__EXTERN List_t *GlbAcpiNodes;

/* Retrieves the version of the 
 * onboard local apic chip, this is 
 * primarily used by the functions here */
uint32_t ApicGetVersion(void)
{
	return (ApicReadLocal(APIC_VERSION) & 0xFF);
}

/* This only is something we need to check on 
 * 32-bit processors, all 64 bit cpus must use
 * the integrated APIC */
int ApicIsIntegrated(void)
{
#ifdef _X86_64
	return 1;
#else
	return (ApicGetVersion() & 0xF0);
#endif
}

/* This function determines if the chip 
 * is modern or 'legacy', but it's not really used
 * here, just in case for the future */
int ApicIsModern(void)
{
	return (ApicGetVersion() >= 0x14) ? 1 : 0;
}

/* Retrieve the max supported LVT for the
 * onboard local apic chip, this is used 
 * for the ESR among others */
int ApicGetMaxLvt(void)
{
	uint32_t Version = ApicReadLocal(APIC_VERSION);
	return APIC_INTEGRATED((Version & 0xFF)) ? (((Version) >> 16) & 0xFF) : 2;
}

/* This function derives an io-apic from
 * the given gsi index, by locating which
 * io-apic owns the gsi and returns it.
 * Returns NULL if gsi is invalid */
IoApic_t *ApicGetIoFromGsi(int Gsi)
{
	/* Locate the io-apic that has
	 * the gsi index in it's range */
	foreach(i, GlbIoApics) {
		IoApic_t *Io = (IoApic_t*)i->Data;
		if (Io->GsiStart <= Gsi &&
			(Io->GsiStart + Io->PinCount) > Gsi) {
			return Io;
		}
	}

	/* Invalid Gsi 
	 * No io-apic! */
	return NULL;
}

/* Calculates the pin from the 
 * given gsi, it tries to locate the
 * relevenat io-apic, if not found 
 * it returns APIC_NO_GSI, otherwise the pin */
int ApicGetPinFromGsi(int Gsi)
{
	/* Convert Gsi to Pin & IoApic */
	foreach(i, GlbIoApics) {
		IoApic_t *Io = (IoApic_t*)i->Data;
		if (Io->GsiStart <= Gsi &&
			(Io->GsiStart + Io->PinCount) > Gsi) {
			return Gsi - Io->GsiStart;
		}
	}

	/* Invalid Gsi 
	 * No io-apic! */
	return APIC_NO_GSI;
}

/* Creates the correct bit index for
 * the given cpu id, and converts the type
 * to uint, since thats what the apic needs */
uint32_t ApicGetCpuMask(UUId_t Cpu)
{
	/* Lets generate the bit */
	return (1 << (uint32_t)Cpu);
}

/* Helper for updating the task priority register
 * this register helps us using Lowest-Priority
 * delivery mode, as this controls which cpu to
 * interrupt */
void ApicSetTaskPriority(uint32_t Priority)
{
	/* Retrieve current value in the
	 * task-prio register */
	uint32_t Temp = ApicReadLocal(APIC_TASK_PRIORITY);

	/* Only clear the priority from the
	 * the value register, then update */
	Temp &= ~(APIC_PRIORITY_MASK);
	Temp |= (Priority & APIC_PRIORITY_MASK);

	/* Rewrite value back */
	ApicWriteLocal(APIC_TASK_PRIORITY, Priority);
}

/* Retrives the current task priority
 * for the current cpu */
uint32_t ApicGetTaskPriority(void)
{
	/* Only return the relevant bits from 
	 * the task register */
	return (ApicReadLocal(APIC_TASK_PRIORITY) & APIC_PRIORITY_MASK);
}

/* ApicMaskGsi 
 * Masks the given gsi if possible by deriving
 * the io-apic and pin from it. This makes sure
 * the io-apic delivers no interrupts */
void ApicMaskGsi(int Gsi)
{
	/* Variables */
	uint64_t Entry = 0;
	IoApic_t *IoApic = NULL;
	int Pin = APIC_NO_GSI;

	/* Get both the io-apic we need
	* to update and the pin */
	IoApic = ApicGetIoFromGsi(Gsi);
	Pin = ApicGetPinFromGsi(Gsi);

	/* Sanitize the lookup */
	if (IoApic == NULL || Pin == APIC_NO_GSI) {
		LogFatal("APIC", "Invalid Gsi %u\n", Gsi);
		return;
	}

	/* Read entry from the io-apic */
	Entry = ApicReadIoEntry(IoApic, Pin);

	/* Unmask */
	Entry |= APIC_MASKED;

	/* Write the entry back */
	ApicWriteIoEntry(IoApic, Pin, Entry);
}

/* ApicUnmaskGsi
 * Unmasks the given gsi if possible by deriving
 * the io-apic and pin from it. This allows the
 * io-apic to deliver interrupts again */
void ApicUnmaskGsi(int Gsi)
{
	/* Variables */
	uint64_t Entry = 0;
	IoApic_t *IoApic = NULL;
	int Pin = APIC_NO_GSI;

	/* Get both the io-apic we need
	* to update and the pin */
	IoApic = ApicGetIoFromGsi(Gsi);
	Pin = ApicGetPinFromGsi(Gsi);

	/* Sanitize the lookup */
	if (IoApic == NULL || Pin == APIC_NO_GSI) {
		LogFatal("APIC", "Invalid Gsi %u\n", Gsi);
		return;
	}

	/* Read entry from the io-apic */
	Entry = ApicReadIoEntry(IoApic, Pin);

	/* Unmask */
	Entry &= ~(APIC_MASKED);

	/* Write the entry back */
	ApicWriteIoEntry(IoApic, Pin, Entry);
}

/* Sends end of interrupt to the local
 * apic chip, and enables for a new interrupt
 * on that irq line to occur */
void ApicSendEoi(int Gsi, uint32_t Vector)
{
	// Some, older (external) chips
	// require more code for sending a proper
	// EOI, but if its new enough then no need
	if (ApicGetVersion() >= 0x10 || Gsi == APIC_NO_GSI) {
		ApicWriteLocal(APIC_INTERRUPT_ACK, Vector);
	}
	else
	{
		/* Damn, it's the DX82 ! 
		 * This means we have to mark the entry
		 * masked and edge in order to clear the IRR */
		uint64_t Modified = 0, Original = 0;
		IoApic_t *IoApic = NULL;
		int Pin = APIC_NO_GSI;

		/* Get both the io-apic we need 
		 * to update and the pin */
		IoApic = ApicGetIoFromGsi(Gsi);
		Pin = ApicGetPinFromGsi(Gsi);

		/* Sanitize the lookup */
		if (IoApic == NULL || Pin == APIC_NO_GSI) {
			LogFatal("APIC", "Invalid Gsi %u\n", Gsi);
			return;
		}

		/* Read Entry */
		Modified = Original = ApicReadIoEntry(IoApic, Pin);

		/* We want to mask it and clear
		 * the level trigger bit */
		Modified |= APIC_MASKED;
		Modified &= ~(APIC_LEVEL_TRIGGER);

		/* First, we write the modified entry
		 * then we restore it, then we ACK */
		ApicWriteIoEntry(IoApic, Pin, Modified);
		ApicWriteIoEntry(IoApic, Pin, Original);
		ApicWriteLocal(APIC_INTERRUPT_ACK, Vector);
	}
}

/* Retrieve the cpu id for the current cpu
 * can be used as an identifier when running
 * multicore */
UUId_t ApicGetCpu(void)
{
	/* Sanitize whether or not the
	 * local apic has been initialized */
	if (GlbLocalApicBase == 0) {
		return 0;
	}
	else {
		return (ApicReadLocal(APIC_PROCESSOR_ID) >> 24) & 0xFF;
	}
}

/* Debug method
 * Print ticks for all available cpus */
void ApicPrintCpuTicks(void)
{
	/* Iterate cpu cores and 
	 * print stats */
	for (int i = 0; i < GlbCpusBooted; i++)
		LogDebug("APIC", "Cpu %i Ticks: %u\n", i, GlbTimerTicks[i]);
}
