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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * x86 Advanced Programmable Interrupt Controller Driver
 *  - Helper functions and utility functions
 */
#define __MODULE "APIC"
#define __TRACE

#include <debug.h>
#include <arch/x86/pic.h>
#include <arch/x86/apic.h>
#include <acpi.h>
#include <string.h>
#include <stdio.h>

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

uint32_t ApicComputeLogicalDestination(uuid_t coreId) {
    return (1 << ((coreId % 7) + 1)) | (1 << 0);
}

/* Helper for updating the task priority register
 * this register helps us using Lowest-Priority
 * delivery mode, as this controls which cpu to
 * interrupt */
void ApicSetTaskPriority(uint32_t priority) {
    uint32_t Temp = ApicReadLocal(APIC_TASK_PRIORITY);
    Temp &= ~(APIC_PRIORITY_MASK);
    Temp |= (priority & APIC_PRIORITY_MASK);
    ApicWriteLocal(APIC_TASK_PRIORITY, priority);
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
    SystemInterruptController_t *Ic = NULL;
    uint64_t Entry  = 0;
    int Pin         = APIC_NO_GSI;

    // Use the correct interrupt mode
    if (GetApicInterruptMode() == InterruptMode_PIC) {
        PicConfigureLine(Gsi, 0, -1);
        return;
    }

    // Use the correct interrupt controller
    Ic  = GetInterruptControllerByLine(Gsi);
    Pin = GetPinOffsetByLine(Gsi);
    if (Ic == NULL || Pin == APIC_NO_GSI) {
        FATAL(FATAL_SCOPE_KERNEL, "Invalid Gsi %" PRIuIN "", Gsi);
    }

    // Update the entry
    Entry = ApicReadIoEntry(Ic, Pin);
    Entry |= APIC_MASKED;
    ApicWriteIoEntry(Ic, Pin, Entry);
}

/* ApicUnmaskGsi
 * Unmasks the given gsi if possible by deriving
 * the io-apic and pin from it. This allows the
 * io-apic to deliver interrupts again */
void ApicUnmaskGsi(int Gsi)
{
    // Variables
    SystemInterruptController_t *Ic = NULL;
    uint64_t Entry  = 0;
    int Pin         = APIC_NO_GSI;

    // Use the correct interrupt mode
    if (GetApicInterruptMode() == InterruptMode_PIC) {
        PicConfigureLine(Gsi, 1, -1);
        return;
    }

    // Use the correct interrupt controller
    Ic  = GetInterruptControllerByLine(Gsi);
    Pin = GetPinOffsetByLine(Gsi);
    if (Ic == NULL || Pin == APIC_NO_GSI) {
        FATAL(FATAL_SCOPE_KERNEL, "Invalid Gsi %" PRIuIN "", Gsi);
    }

    // Update the entry
    Entry = ApicReadIoEntry(Ic, Pin);
    Entry &= ~(APIC_MASKED);
    ApicWriteIoEntry(Ic, Pin, Entry);
}

/* Sends end of interrupt to the local
 * apic chip, and enables for a new interrupt
 * on that irq line to occur */
void ApicSendEoi(int Gsi, uint32_t Vector)
{
    // Use the correct interrupt mode
    if (GetApicInterruptMode() == InterruptMode_PIC) {
        if (Gsi != APIC_NO_GSI) {
            PicSendEoi(Gsi);
        }
        ApicWriteLocal(APIC_INTERRUPT_ACK, 0);
        return;
    }

    // Some, older (external) chips
    // require more code for sending a proper
    // EOI, but if its new enough then no need
    if (ApicGetVersion() >= 0x10 || Gsi == APIC_NO_GSI) {
        // @todo, x2APIC requires a write of 0
        // what is differnt from apic
        ApicWriteLocal(APIC_INTERRUPT_ACK, 0);
    }
    else
    {
        // Damn, it's the DX82 ! This means we have to mark the entry
        // masked and edge in order to clear the IRR
        SystemInterruptController_t *Ic = NULL;
        uint64_t Modified = 0, Original = 0;
        int Pin = APIC_NO_GSI;

        // Use the correct interrupt controller
        Ic  = GetInterruptControllerByLine(Gsi);
        Pin = GetPinOffsetByLine(Gsi);
        if (Ic == NULL || Pin == APIC_NO_GSI) {
            FATAL(FATAL_SCOPE_KERNEL, "Invalid Gsi %" PRIuIN "", Gsi);
        }

        // We want to mask it and clear the level trigger bit
        Modified = Original = ApicReadIoEntry(Ic, Pin);
        Modified |= APIC_MASKED;
        Modified &= ~(APIC_LEVEL_TRIGGER | APIC_DELIVERY_BUSY);
        Original &= ~(APIC_DELIVERY_BUSY);

        // First, we write the modified entry
        // then we restore it, then we ACK
        ApicWriteIoEntry(Ic, Pin, Modified);
        ApicWriteIoEntry(Ic, Pin, Original);
        
        // @todo, x2APIC requires a write of 0
        // what is differnt from apic
        ApicWriteLocal(APIC_INTERRUPT_ACK, 0);
    }
}
