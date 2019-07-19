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
 * MollenOS x86 Common Timer Interface
 * - Contains shared x86 timer routines
 */
#define __MODULE "TMIF"
#define __TRACE

#include <acpiinterface.h>
#include <interrupts.h>
#include <debug.h>
#include <apic.h>
#include <cpu.h>
#include "../cmos.h"
#include "../pit.h"

OsStatus_t
TimersDiscover(void)
{
    int RtcAvailable = 1;
    
    TRACE("TimersDiscover()");

    // Start out by detecting presence of HPET
    if (AcpiAvailable() == ACPI_AVAILABLE) {
        if (AcpiGbl_FADT.BootFlags & ACPI_FADT_NO_CMOS_RTC) {
            RtcAvailable = 0;
        }
    }
	
    // Do we have an RTC?
    if (RtcAvailable == 0) {
        if (PitInitialize() != OsSuccess) {
            ERROR("Failed to initialize the PIT.");
        }
    }
    return CmosInitialize(RtcAvailable);
}

OsStatus_t
InitializeSystemTimers(void)
{
    // Free all the allocated isa's now for drivers
    InterruptDecreasePenalty(0); // PIT
    InterruptDecreasePenalty(1); // PS/2
    InterruptDecreasePenalty(2); // PIT / Cascade
    InterruptDecreasePenalty(3); // COM 2/4
    InterruptDecreasePenalty(4); // COM 1/3
    InterruptDecreasePenalty(5); // LPT2
    InterruptDecreasePenalty(6); // Floppy
    InterruptDecreasePenalty(7); // LPT1 / Spurious
    InterruptDecreasePenalty(8); // CMOS
    InterruptDecreasePenalty(12); // PS/2
    InterruptDecreasePenalty(13); // FPU
    InterruptDecreasePenalty(14); // IDE
    InterruptDecreasePenalty(15); // IDE

    // Activate fixed system timers
    if (TimersDiscover() != OsSuccess) {
        return OsError;
    }
    
    // Recalibrate in case of apic
    if (CpuHasFeatures(0, CPUID_FEAT_EDX_APIC) == OsSuccess) {
        ApicRecalibrateTimer();
    }
    return OsSuccess;
}
