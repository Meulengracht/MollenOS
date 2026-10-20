/**
 * Copyright 2011, Philip Meulengracht
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
 * x86 Cpu Information Header
 * - Contains some definitions and structures for helping around
 *   in the sub-layer system
 */

#define __TRACE

#include <component/timer.h>
#include <arch/x86/cpu.h>
#include <arch/x86/tsc.h>
#include <debug.h>
#include <machine.h>

extern void _rdtsc(uint64_t *Value);

static void TscGetCount(void*, UInteger64_t*);
static void TscGetFrequency(void*, UInteger64_t*);
static void TscNoOperation(void*);

// Recalibrate is registered as a no-op as the TSC never needs to be calibrated again
static tick_t                  g_tscFrequency = 0;
static SystemTimerOperations_t g_tscOperations = {
        .Enable = NULL,
        .Configure = NULL,
        .GetFrequencyRange = NULL,
        .Read = TscGetCount,
        .GetFrequency = TscGetFrequency,
        .Recalibrate = TscNoOperation
};

static oserr_t
__Calibrate(void)
{
    uint64_t     tscStart, tscEnd;
    UInteger64_t tick, frequency, tickStart;
    uint64_t     interval;

    SystemTimerGetClockFrequency(&frequency);
    if (frequency.QuadPart == 0) {
        return OS_ENOTSUPPORTED;
    }

    interval = frequency.QuadPart / 10;
    if (interval == 0) {
        interval = 1;
    }

    // Start at a reference tick boundary so an IRQ clock's partial first
    // period is not mistaken for a full calibration tick.
    SystemTimerGetClockTick(&tickStart);
    do {
        SystemTimerGetClockTick(&tick);
    } while (tick.QuadPart == tickStart.QuadPart);
    tickStart = tick;
    _rdtsc(&tscStart);

    // Run for 100ms, so divide the frequency (ticks per second) by 10
    do {
        SystemTimerGetClockTick(&tick);
    } while (tick.QuadPart - tickStart.QuadPart < interval);

    // get end tick
    _rdtsc(&tscEnd);

    // calculate the frequency
    g_tscFrequency = ((tscEnd - tscStart) * frequency.QuadPart) /
            (tick.QuadPart - tickStart.QuadPart);
    return g_tscFrequency ? OS_EOK : OS_ENOTSUPPORTED;
}

void
TscInitialize(void)
{
    oserr_t oserr;
    TRACE("TscInitialize()");

    if (CpuHasFeatures(0, CPUID_FEAT_EDX_TSC) != OS_EOK) {
        WARNING("TscInitialize TSC is not available for this CPU");
        return;
    }
    
    if (!(GetMachine()->Processor.PlatformData.Flags & X86_CPU_FLAG_INVARIANT_TSC)) {
        WARNING("TscInitialize the TSC is non-invariant, will not use it for time-keeping");
        return;
    }

    // Calibrate the TSC against other system timers.
    if (__Calibrate() != OS_EOK) {
        WARNING("TscInitialize no usable calibration reference");
        return;
    }

    // register as available platform timer
    oserr = SystemTimerRegister(
        "x86-tsc",
        &g_tscOperations,
        SystemTimeAttributes_COUNTER | SystemTimeAttributes_CALIBRATED,
        NULL
    );
    if (oserr != OS_EOK) {
        WARNING("TscInitialize failed to register platform timer");
    }
}

static void
TscGetCount(
    _In_ void*         context,
    _In_ UInteger64_t* tick)
{
    _CRT_UNUSED(context);
    _rdtsc(&tick->QuadPart);
}

static void
TscGetFrequency(
    _In_ void*         context,
    _In_ UInteger64_t* frequency)
{
    _CRT_UNUSED(context);
    frequency->QuadPart = g_tscFrequency;
}

static void
TscNoOperation(
    _In_ void* context)
{
    // no-op
    _CRT_UNUSED(context);
}
