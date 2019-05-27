/* MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * Interrupt Interface
 * - Contains the shared kernel interrupt interface
 *   that all sub-layers must conform to
 *
 * - ISA Interrupts should be routed to boot-processor without lowest-prio?
 */
#define __MODULE        "IRQS"
//#define __TRACE

#include <interrupts.h>
#include <thread.h>
#include <debug.h>
#include <apic.h>
#include <arch.h>
#include <gdt.h>
#include <cpu.h>

void
InitializeSoftwareInterrupts(void)
{
    DeviceInterrupt_t Interrupt = { { 0 } };
    Interrupt.Vectors[0]            = INTERRUPT_NONE;
    Interrupt.Pin                   = INTERRUPT_NONE;
    
    // Install local apic handlers
    // - LVT Error handler
    Interrupt.Line                  = INTERRUPT_LVTERROR;
    Interrupt.FastInterrupt.Handler = ApicErrorHandler;
    InterruptRegister(&Interrupt, INTERRUPT_SOFT | INTERRUPT_KERNEL 
        | INTERRUPT_NOTSHARABLE);
    
    // - Timer handler
    Interrupt.Line                  = INTERRUPT_LAPIC;
    Interrupt.FastInterrupt.Handler = ApicTimerHandler;
    InterruptRegister(&Interrupt, INTERRUPT_SOFT | INTERRUPT_KERNEL 
        | INTERRUPT_NOTSHARABLE);
}

OsStatus_t
InterruptInitialize(void)
{
    InitializeSoftwareInterrupts();
#if defined(amd64) || defined(__amd64__)
    TssCreateStacks();
#endif

    // Make sure we allocate all device interrupts
    // so system can't take control of them
    InterruptIncreasePenalty(0);    // PIT
    InterruptIncreasePenalty(1);    // PS/2
    InterruptIncreasePenalty(2);    // PIT / Cascade
    InterruptIncreasePenalty(3);    // COM 2/4
    InterruptIncreasePenalty(4);    // COM 1/3
    InterruptIncreasePenalty(5);    // LPT2
    InterruptIncreasePenalty(6);    // Floppy
    InterruptIncreasePenalty(7);    // LPT1 / Spurious
    InterruptIncreasePenalty(8);    // CMOS
    InterruptIncreasePenalty(12);   // PS/2
    InterruptIncreasePenalty(13);   // FPU
    InterruptIncreasePenalty(14);   // IDE
    InterruptIncreasePenalty(15);   // IDE / Spurious
    
    // Initialize APIC?
    if (CpuHasFeatures(0, CPUID_FEAT_EDX_APIC) == OsSuccess) {
        ApicInitialize();
    }
    else {
        ERROR(" > cpu does not have a local apic. This model is too old and not supported.");
        return OsError;
    }
    return OsSuccess;
}
