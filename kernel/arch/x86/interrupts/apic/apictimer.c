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
 *
 * X86 Advanced Programmable Interrupt Controller Driver
 *  - Initialization code for boot/ap cpus
 */
#define __MODULE "APIC"
#define __TRACE

#define __need_minmax
#include <assert.h>
#include <arch/io.h>
#include <arch/x86/arch.h>
#include <arch/x86/apic.h>
#include <arch/x86/cpu.h>
#include <arch/interrupts.h>
#include <component/timer.h>
#include <debug.h>
#include <threading.h>

typedef struct LocalApicTimer {
    uint64_t Frequency;
} LocalApicTimer_t;

static void ApicTimerRecalibrate(void*);

// TODO this should be per-core
static LocalApicTimer_t g_lapicTimer = {
    .Frequency = 0
};

static uint32_t
__DeadlineToTicks(clock_t nanoseconds)
{
    uint64_t maximum = (UINT32_MAX * (uint64_t)NSEC_PER_SEC) / g_lapicTimer.Frequency;
    
    // The scheduler uses zero to mean there is no pending deadline.
    if (nanoseconds == 0) {
        return 0;
    }
    
    if ((uint64_t)nanoseconds >= maximum) {
        return UINT32_MAX;
    }

    // Round up: a positive sub-tick deadline must never disable the timer.
    return (uint32_t)(((uint64_t)nanoseconds * g_lapicTimer.Frequency + NSEC_PER_SEC - 1) / NSEC_PER_SEC);
}

irqstatus_t
ApicTimerHandler(
    _In_ InterruptFunctionTable_t* NotUsed,
    _In_ void*                     Context)
{
    _CRT_UNUSED(NotUsed);
    _CRT_UNUSED(Context);

    uint32_t tick         = ApicReadLocal(APIC_CURRENT_COUNT);
    uint32_t ticksPassed  = ApicReadLocal(APIC_INITIAL_COUNT) - tick;
    clock_t  nextDeadline = 20 * NSEC_PER_MSEC;

    // clear the initial count if we were interrupted
    if (tick != 0) {
        ApicWriteLocal(APIC_INITIAL_COUNT, 0);
    }

    // call the threading code, the local apic timer is the one we use for thread
    // switching
    (void)ThreadingAdvance(
        tick == 0 ? 1 : 0,
        ((uint64_t)ticksPassed * NSEC_PER_SEC) / g_lapicTimer.Frequency,
        &nextDeadline
    );

    // restart timer
    ApicWriteLocal(
        APIC_INITIAL_COUNT,
        __DeadlineToTicks(nextDeadline)
    );
    return IRQSTATUS_HANDLED;
}

void
ApicTimerInitialize(void)
{
    TRACE("ApicTimerInitialize()");

    if (CpuHasFeatures(0, CPUID_FEAT_EDX_APIC) != OS_EOK) {
        WARNING("ApicTimerInitialize lapic timer not supported");
        return;
    }

    // Run the calibration code
    ApicTimerRecalibrate(NULL);

    // Start timer for good
    ApicTimerStart(__DeadlineToTicks(20 * NSEC_PER_MSEC));
}

void
ApicTimerStart(
    _In_ size_t quantumValue)
{
    ApicWriteLocal(APIC_TIMER_VECTOR,    APIC_TIMER_ONESHOT | INTERRUPT_LAPIC);
    ApicWriteLocal(APIC_DIVIDE_REGISTER, APIC_TIMER_DIVIDER_1);
    ApicWriteLocal(APIC_INITIAL_COUNT, quantumValue);
}

static void
ApicTimerRecalibrate(
    _In_ void* context)
{
    clock_t      lapicTicks;
    UInteger64_t frequency, start, tick;
    uint64_t     interval;
    _CRT_UNUSED(context);

    SystemTimerGetClockFrequency(&frequency);
    if (frequency.QuadPart == 0) {
        FATAL(FATAL_SCOPE_KERNEL, "APIC timer requires a calibration clock");
    }
    
    interval = MAX(1, frequency.QuadPart / 10);
    SystemTimerGetClockTick(&start);
    do {
        SystemTimerGetClockTick(&tick);
    } while (tick.QuadPart == start.QuadPart);
    start = tick;

    // Setup initial local apic timer registers
    // TODO handle the case where the apic timer ticks WAY to quick for the 1 divider
    ApicWriteLocal(APIC_TIMER_VECTOR,    INTERRUPT_LAPIC);
    ApicWriteLocal(APIC_DIVIDE_REGISTER, APIC_TIMER_DIVIDER_1);
    ApicWriteLocal(APIC_INITIAL_COUNT,   0xFFFFFFFF); // Set counter to max, it counts down

    // Sleep for 100 ms
    do {
        SystemTimerGetClockTick(&tick);
    } while (tick.QuadPart - start.QuadPart < interval);

    // Stop counter and calibrate the frequency of the lapic
    lapicTicks = (0xFFFFFFFFU - ApicReadLocal(APIC_CURRENT_COUNT));
    assert(lapicTicks != 0xFFFFFFFF);
    ApicWriteLocal(APIC_TIMER_VECTOR, APIC_MASKED);

    lapicTicks = ((uint64_t)lapicTicks * frequency.QuadPart) / (tick.QuadPart - start.QuadPart);
    if (lapicTicks == 0) {
        FATAL(FATAL_SCOPE_KERNEL, "APIC timer calibration returned zero frequency");
    }

    // Reset the timer data
    g_lapicTimer.Frequency = lapicTicks;

    // calculate the quantum
    TRACE("ApicTimerRecalibrate BusSpeed: %" PRIuIN " Hz", lapicTicks);
}
