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
#define __MODULE "APIC"
#define __TRACE

/* Includes 
 * - System */
#include <debug.h>
#include <apic.h>
#include <acpi.h>
#include <pic.h>

/* Includes
 * - Library */
#include <ds/collection.h>
#include <string.h>
#include <stdio.h>

/* Externs, we need access to a lot of different
 * things for these helper functions */
__EXTERN Collection_t *GlbIoApics;

/* Retrieves the version of the 
 * onboard local apic chip, this is 
 * primarily used by the functions here */
uint32_t ApicGetVersion(void) {
	return (ApicReadLocal(APIC_VERSION) & 0xFF);
}

/* This only is something we need to check on 
 * 32-bit processors, all 64 bit cpus must use
 * the integrated APIC */
int ApicIsIntegrated(void) {
#if defined(__x86_64__) || defined(amd64) || defined(__amd64__)
	return 1;
#else
	return (ApicGetVersion() & 0xF0);
#endif
}

/* This function determines if the chip 
 * is modern or 'legacy', but it's not really used
 * here, just in case for the future */
int ApicIsModern(void) {
	return (ApicGetVersion() >= 0x14) ? 1 : 0;
}

/* Retrieve the max supported LVT for the
 * onboard local apic chip, this is used 
 * for the ESR among others */
int ApicGetMaxLvt(void) {
	uint32_t Version = ApicReadLocal(APIC_VERSION);
	return APIC_INTEGRATED((Version & 0xFF)) ? (((Version) >> 16) & 0xFF) : 2;
}

/* This function derives an io-apic from
 * the given gsi index, by locating which
 * io-apic owns the gsi and returns it.
 * Returns NULL if gsi is invalid */
IoApic_t *ApicGetIoFromGsi(int Gsi) {
	foreach(i, GlbIoApics) {
		IoApic_t *Io = (IoApic_t*)i->Data;
		if (Io->GsiStart <= Gsi && (Io->GsiStart + Io->PinCount) > Gsi) {
			return Io;
		}
	}
	return NULL;
}

/* Calculates the pin from the 
 * given gsi, it tries to locate the
 * relevenat io-apic, if not found 
 * it returns APIC_NO_GSI, otherwise the pin */
int ApicGetPinFromGsi(int Gsi) {
	foreach(i, GlbIoApics) {
		IoApic_t *Io = (IoApic_t*)i->Data;
		if (Io->GsiStart <= Gsi &&
			(Io->GsiStart + Io->PinCount) > Gsi) {
			return Gsi - Io->GsiStart;
		}
	}
	return APIC_NO_GSI;
}

/* ApicComputeLogicalDestination
 * Creates the correct bit index for the given cpu core */
uint32_t ApicComputeLogicalDestination(UUId_t CoreId) {
	return (1 << ((CoreId % 7) + 1)) | (1 << 0);
}

/* Helper for updating the task priority register
 * this register helps us using Lowest-Priority
 * delivery mode, as this controls which cpu to
 * interrupt */
void ApicSetTaskPriority(uint32_t Priority) {
	uint32_t Temp = ApicReadLocal(APIC_TASK_PRIORITY);
	Temp &= ~(APIC_PRIORITY_MASK);
	Temp |= (Priority & APIC_PRIORITY_MASK);
	ApicWriteLocal(APIC_TASK_PRIORITY, Priority);
}

/* Retrives the current task priority
 * for the current cpu */
uint32_t ApicGetTaskPriority(void) {
	return (ApicReadLocal(APIC_TASK_PRIORITY) & APIC_PRIORITY_MASK);
}

/* ApicMaskGsi 
 * Masks the given gsi if possible by deriving
 * the io-apic and pin from it. This makes sure
 * the io-apic delivers no interrupts */
void ApicMaskGsi(int Gsi)
{
	// Variables
	IoApic_t *IoApic    = NULL;
	uint64_t Entry      = 0;
	int Pin             = APIC_NO_GSI;

    // Lookup the correct io-apic
    if (GlbIoApics != NULL) {
        IoApic  = ApicGetIoFromGsi(Gsi);
        Pin     = ApicGetPinFromGsi(Gsi);
        if (IoApic == NULL || Pin == APIC_NO_GSI) {
            FATAL(FATAL_SCOPE_KERNEL, "Invalid Gsi %u", Gsi);
            return;
        }
        Entry = ApicReadIoEntry(IoApic, Pin);
        Entry |= APIC_MASKED;
        ApicWriteIoEntry(IoApic, Pin, Entry);
    }
    else {
        PicConfigureLine(Gsi, 0, -1);
    }
}

/* ApicUnmaskGsi
 * Unmasks the given gsi if possible by deriving
 * the io-apic and pin from it. This allows the
 * io-apic to deliver interrupts again */
void ApicUnmaskGsi(int Gsi)
{
    // Variables
	IoApic_t *IoApic    = NULL;
	uint64_t Entry      = 0;
	int Pin             = APIC_NO_GSI;

    // Lookup the correct io-apic
    if (GlbIoApics != NULL) {
        IoApic  = ApicGetIoFromGsi(Gsi);
        Pin     = ApicGetPinFromGsi(Gsi);
        if (IoApic == NULL || Pin == APIC_NO_GSI) {
            FATAL(FATAL_SCOPE_KERNEL, "Invalid Gsi %u", Gsi);
            return;
        }
        Entry = ApicReadIoEntry(IoApic, Pin);
	    Entry &= ~(APIC_MASKED);
        ApicWriteIoEntry(IoApic, Pin, Entry);
    }
    else {
        PicConfigureLine(Gsi, 1, -1);
    }
}

/* Sends end of interrupt to the local
 * apic chip, and enables for a new interrupt
 * on that irq line to occur */
void ApicSendEoi(int Gsi, uint32_t Vector)
{
    // Old-school pic
    if (GlbIoApics == NULL) {
        if (Gsi != APIC_NO_GSI) {
            PicSendEoi(Gsi);
        }
		ApicWriteLocal(APIC_INTERRUPT_ACK, 0);
    }

	// Some, older (external) chips
	// require more code for sending a proper
	// EOI, but if its new enough then no need
	else if (ApicGetVersion() >= 0x10 || Gsi == APIC_NO_GSI) {
        // @todo, x2APIC requires a write of 0
		ApicWriteLocal(APIC_INTERRUPT_ACK, 0);
	}
	else
	{
		/* Damn, it's the DX82 ! 
		 * This means we have to mark the entry
		 * masked and edge in order to clear the IRR */
		uint64_t Modified = 0, Original = 0;
		IoApic_t *IoApic = NULL;
		int Pin = APIC_NO_GSI;

		// Lookup io-apic conf
		IoApic  = ApicGetIoFromGsi(Gsi);
		Pin     = ApicGetPinFromGsi(Gsi);
		if (IoApic == NULL || Pin == APIC_NO_GSI) {
			FATAL(FATAL_SCOPE_KERNEL, "Invalid Gsi %u", Gsi);
			return;
		}

		/* We want to mask it and clear the level trigger bit */
        Modified = Original = ApicReadIoEntry(IoApic, Pin);
		Modified |= APIC_MASKED;
        Modified &= ~(APIC_LEVEL_TRIGGER | APIC_DELIVERY_BUSY);
        Original &= ~(APIC_DELIVERY_BUSY);

		/* First, we write the modified entry
		 * then we restore it, then we ACK */
		ApicWriteIoEntry(IoApic, Pin, Modified);
        ApicWriteIoEntry(IoApic, Pin, Original);
        
        // @todo, x2APIC requires a write of 0
		ApicWriteLocal(APIC_INTERRUPT_ACK, 0);
	}
}
