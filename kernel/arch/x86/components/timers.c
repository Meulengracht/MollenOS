/**
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
 * x86 Common Timer Interface
 * - Contains shared x86 timer routines
 */

#define __TRACE

#include <acpiinterface.h>
#include <arch/x86/apic.h>
#include <arch/x86/cpu.h>
#include <arch/x86/cmos.h>
#include <arch/x86/pit.h>
#include <arch/x86/tsc.h>
#include <debug.h>
#include <interrupts.h>

uint32_t g_calibrationTick = 0;

oserr_t
TimersDiscover(void)
{
    oserr_t osStatus;
    int        rtcAvailable = 1;
    
    TRACE("TimersDiscover()");

    // Start out by detecting presence of HPET
    if (AcpiAvailable() == ACPI_AVAILABLE) {
        if (AcpiGbl_FADT.BootFlags & ACPI_FADT_NO_CMOS_RTC) {
            WARNING("TimersDiscover RTC is not available on this platform");
            rtcAvailable = 0;
        }
    }

    // we use the RTC primarily if it's available, for both calibration stage and
    // the time-keeping.
    osStatus = CmosInitialize(rtcAvailable);
    if (osStatus != OS_EOK) {
        WARNING("TimersDiscover failed to initialize the CMOS or RTC");
    }

    // if the RTC should not be available, then we must resort to the PIT if that is available.
    // otherwise, we require the HPET to be available.
    osStatus = PitInitialize(rtcAvailable);
    if (osStatus != OS_EOK) {
        WARNING("TimersDiscover failed to initialize the PIT");
    }
    return osStatus;
}

oserr_t
PlatformTimersInitialize(void)
{
    oserr_t osStatus;

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
    osStatus = TimersDiscover();
    if (osStatus != OS_EOK) {
        return osStatus;
    }

    // Calibrate the TSC
    TscInitialize();
    
    // Calibrate the Local Apic Timer
    ApicTimerInitialize();

    // Disable calibration mode
    RtcSetCalibrationMode(0);
    PitSetCalibrationMode(0);
    return OS_EOK;
}
