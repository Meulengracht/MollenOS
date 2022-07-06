/**
 * MollenOS
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Interrupt Interface
 * - Contains the shared kernel interrupt interface
 *   that all sub-layers must conform to
 *
 * - ISA Interrupts should be routed to boot-processor without lowest-prio?
 */

#define __TRACE

#include <arch/x86/arch.h>
#include <arch/x86/apic.h>
#include <arch/x86/cpu.h>
#include <interrupts.h>
#include <debug.h>

static void
__InstallSoftwareHandlers(void)
{
    DeviceInterrupt_t deviceInterrupt = {{0 } };
    TRACE("__InstallSoftwareHandlers()");

    deviceInterrupt.Vectors[0] = INTERRUPT_NONE;
    deviceInterrupt.Pin = INTERRUPT_NONE;
    
    // Install local apic handlers
    // - LVT Error handler
    deviceInterrupt.Line                  = INTERRUPT_LVTERROR;
    deviceInterrupt.ResourceTable.Handler = ApicErrorHandler;
    InterruptRegister(
            &deviceInterrupt,
            INTERRUPT_SOFT | INTERRUPT_KERNEL | INTERRUPT_EXCLUSIVE
    );
    
    // - Timer handler
    deviceInterrupt.Line                  = INTERRUPT_LAPIC;
    deviceInterrupt.ResourceTable.Handler = ApicTimerHandler;
    InterruptRegister(
            &deviceInterrupt,
            INTERRUPT_SOFT | INTERRUPT_KERNEL | INTERRUPT_EXCLUSIVE
    );
}

oscode_t
PlatformInterruptInitialize(void)
{
    TRACE("PlatformInterruptInitialize()");
    __InstallSoftwareHandlers();

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
    if (CpuHasFeatures(0, CPUID_FEAT_EDX_APIC) == OsOK) {
        ApicInitialize();
    }
    else {
        ERROR("PlatformInterruptInitialize cpu does not have a local apic. This model is too old and not supported.");
        return OsNotSupported;
    }
    return OsOK;
}
