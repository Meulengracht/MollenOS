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

#include <component/timer.h>
#include <arch/x86/cpu.h>
#include <arch/x86/tsc.h>
#include <ddk/io.h>
#include <debug.h>

// import the calibration ticker
extern uint32_t g_calibrationTick;
extern void _rdtsc(uint64_t *Value);

static void TscGetCount(void*, LargeUInteger_t*);
static void TscGetFrequency(void*, LargeUInteger_t*);
static void TscNoOperation(void*);

/**
 * Recalibrate is registered as a no-op as the TSC never needs to be calibrated again
 */
static SystemTimerOperations_t g_tscOperations = {
        .Read = TscGetCount,
        .GetFrequency = TscGetFrequency,
        .Recalibrate = TscNoOperation
};
static tick_t g_tscFrequency = 0;

void
TscInitialize(void)
{
    OsStatus_t osStatus;
    uint64_t   tscStart;
    uint64_t   tscEnd;
    uint32_t   ticker;
    uint32_t   tickEnd;
    TRACE("TscInitialize()");

    if (CpuHasFeatures(0, CPUID_FEAT_EDX_TSC) != OsSuccess) {
        WARNING("TscInitialize TSC is not available for this CPU");
        return;
    }

    // Use the read timestamp counter
    ticker = READ_VOLATILE(g_calibrationTick);
    _rdtsc(&tscStart);

    // wait 100 ms
    tickEnd = ticker + 100;
    while (ticker < tickEnd) {
        ticker = READ_VOLATILE(g_calibrationTick);
    }
    _rdtsc(&tscEnd);

    // calculate the frequency
    tscEnd -= tscStart;
    tscEnd *= 10; // ticks per second
    g_tscFrequency = tscEnd;

    // register as available platform timer
    osStatus = SystemTimerRegister(
            &g_tscOperations,
            SystemTimeAttributes_COUNTER | SystemTimeAttributes_CALIBRATED,
            UUID_INVALID,
            NULL);
    if (osStatus != OsSuccess) {
        WARNING("TscInitialize failed to register platform timer");
    }
}

static void
TscGetCount(
        _In_ void*            context,
        _In_ LargeUInteger_t* tick)
{
    _CRT_UNUSED(context);
    _rdtsc(&tick->QuadPart);
}

static void
TscGetFrequency(
        _In_ void*            context,
        _In_ LargeUInteger_t* frequency)
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
