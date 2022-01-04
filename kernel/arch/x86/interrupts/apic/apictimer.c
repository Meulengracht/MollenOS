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

#include <arch/io.h>
#include <arch/x86/arch.h>
#include <arch/x86/apic.h>
#include <arch/x86/cpu.h>
#include <acpiinterface.h>
#include <component/timer.h>
#include <debug.h>
#include <arch/interrupts.h>
#include <threading.h>

typedef struct LocalApicTimer {
    uint32_t Quantum;
    uint64_t Frequency;
    uint64_t Tick;
} LocalApicTimer_t;

// import the calibration ticker
extern uint32_t g_calibrationTick;

static void ApicTimerGetCount(void*, LargeUInteger_t*);
static void ApicTimerGetFrequency(void*, LargeUInteger_t*);
static void ApicTimerRecalibrate(void*);

// TODO this should be per-core
static LocalApicTimer_t g_lapicTimer = { .Quantum = APIC_DEFAULT_QUANTUM, 0, 0 };

static SystemTimerOperations_t g_lapicOperations = {
        .Read = ApicTimerGetCount,
        .GetFrequency = ApicTimerGetFrequency,
        .Recalibrate = ApicTimerRecalibrate
};

InterruptStatus_t
ApicTimerHandler(
        _In_ InterruptFunctionTable_t* NotUsed,
        _In_ void*                     Context)
{
    _CRT_UNUSED(NotUsed);
    _CRT_UNUSED(Context);

    uint32_t tick         = ApicReadLocal(APIC_CURRENT_COUNT);
    uint32_t ticksPassed  = ApicReadLocal(APIC_INITIAL_COUNT) - tick;
    size_t   nextDeadline = 20;

    // clear the initial count if we were interrupted
    if (tick != 0) {
        ApicWriteLocal(APIC_INITIAL_COUNT, 0);
    }

    // Increase the global tick-counter
    g_lapicTimer.Tick += ticksPassed;

    // call the threading code, the local apic timer is the one we use for thread
    // switching
    (void)ThreadingAdvance(
            tick == 0 ? 1 : 0,
            DIVUP(ticksPassed, g_lapicTimer.Quantum),
            &nextDeadline
    );

    // restart timer
    ApicWriteLocal(
            APIC_INITIAL_COUNT,
            g_lapicTimer.Quantum * nextDeadline
    );
    return InterruptHandled;
}

void
ApicTimerInitialize(void)
{
    OsStatus_t osStatus;
    TRACE("ApicTimerInitialize()");

    if (CpuHasFeatures(0, CPUID_FEAT_EDX_APIC) != OsSuccess) {
        WARNING("ApicTimerInitialize lapic timer not supported");
        return;
    }

    // Run the calibration code
    ApicTimerRecalibrate(NULL);

    // register as available platform timer
    osStatus = SystemTimerRegister(
            &g_lapicOperations,
            SystemTimeAttributes_COUNTER | SystemTimeAttributes_IRQ | SystemTimeAttributes_CALIBRATED,
            UUID_INVALID,
            NULL);
    if (osStatus != OsSuccess) {
        WARNING("TscInitialize failed to register platform timer");
    }

    // Start timer for good
    ApicTimerStart(g_lapicTimer.Quantum * 20);
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
ApicTimerGetCount(
        _In_ void*            context,
        _In_ LargeUInteger_t* count)
{
    // So we need to get the number of ticks passed for the current timeslice, otherwise
    // we will return the same value each time a thread calls this function, which we do
    // not want. However, what we should do here to avoid any data-races with switches
    // we should disable irqs while reading this
    IntStatus_t intStatus   = InterruptDisable();
    uint32_t    tick        = ApicReadLocal(APIC_CURRENT_COUNT);
    uint32_t    ticksPassed = ApicReadLocal(APIC_INITIAL_COUNT) - tick;
    _CRT_UNUSED(context);

    count->QuadPart = g_lapicTimer.Tick + ticksPassed;
    InterruptRestoreState(intStatus);
}

static void
ApicTimerGetFrequency(
        _In_ void*            context,
        _In_ LargeUInteger_t* frequency)
{
    _CRT_UNUSED(context);
    frequency->QuadPart = g_lapicTimer.Frequency;
}

static void
ApicTimerRecalibrate(
        _In_ void* context)
{
    uint64_t lapicTicks;
    uint32_t ticker;
    uint32_t tickEnd;

    _CRT_UNUSED(context);

    // Setup initial local apic timer registers
    ApicWriteLocal(APIC_TIMER_VECTOR,    INTERRUPT_LAPIC);
    ApicWriteLocal(APIC_DIVIDE_REGISTER, APIC_TIMER_DIVIDER_1);
    ApicWriteLocal(APIC_INITIAL_COUNT,   0xFFFFFFFF); // Set counter to max, it counts down

    // Sleep for 100 ms
    ticker = READ_VOLATILE(g_calibrationTick);
    tickEnd = ticker + 100;
    while (ticker < tickEnd) {
        ticker = READ_VOLATILE(g_calibrationTick);
    }

    // Stop counter and calibrate the frequency of the lapic
    ApicWriteLocal(APIC_TIMER_VECTOR, APIC_MASKED);
    lapicTicks = (0xFFFFFFFFU - ApicReadLocal(APIC_CURRENT_COUNT));
    lapicTicks *= 10; // ticks per second

    // Reset the timer data
    g_lapicTimer.Tick = 0;
    g_lapicTimer.Frequency = lapicTicks;
    g_lapicTimer.Quantum = (lapicTicks / 1000) + 1;

    // calculate the quantum
    TRACE("ApicTimerRecalibrate BusSpeed: %" PRIuIN " Hz", lapicTicks);
    TRACE("ApicTimerRecalibrate ApicQuantum(1ms): %" PRIuIN "", g_lapicTimer.Quantum);
}
