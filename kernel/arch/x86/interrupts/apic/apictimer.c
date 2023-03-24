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
    uint32_t Quantum;
    uint32_t QuantumUnit;
    uint64_t Frequency;
    uint64_t Tick;
} LocalApicTimer_t;

static void ApicTimerGetCount(void*, UInteger64_t*);
static void ApicTimerGetFrequency(void*, UInteger64_t*);
static void ApicTimerRecalibrate(void*);

// TODO this should be per-core
static LocalApicTimer_t g_lapicTimer = {
        .Quantum = APIC_DEFAULT_QUANTUM,
        .QuantumUnit = 0,
        .Frequency = 0,
        .Tick = 0
};

static SystemTimerOperations_t g_lapicOperations = {
        .Read = ApicTimerGetCount,
        .GetFrequency = ApicTimerGetFrequency,
        .Recalibrate = ApicTimerRecalibrate
};

irqstatus_t
ApicTimerHandler(
        _In_ InterruptFunctionTable_t* NotUsed,
        _In_ void*                     Context)
{
    _CRT_UNUSED(NotUsed);
    _CRT_UNUSED(Context);

    uint32_t tick         = ApicReadLocal(APIC_CURRENT_COUNT);
    uint32_t ticksPassed  = ApicReadLocal(APIC_INITIAL_COUNT) - tick;
    clock_t  nextDeadline = (20 * NSEC_PER_MSEC) / g_lapicTimer.QuantumUnit; // 20ms baseline

    // clear the initial count if we were interrupted
    if (tick != 0) {
        ApicWriteLocal(APIC_INITIAL_COUNT, 0);
    }

    // Increase the global tick-counter
    //g_lapicTimer.Tick += ticksPassed;

    // call the threading code, the local apic timer is the one we use for thread
    // switching
    (void)ThreadingAdvance(
            tick == 0 ? 1 : 0,
            (((clock_t)ticksPassed / (clock_t)g_lapicTimer.Quantum) * (clock_t)g_lapicTimer.QuantumUnit),
            &nextDeadline
    );

    // restart timer
    ApicWriteLocal(
            APIC_INITIAL_COUNT,
            MIN(g_lapicTimer.Quantum * (nextDeadline / g_lapicTimer.QuantumUnit), 0xFFFFFFFF)
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
        _In_ UInteger64_t* count)
{
    // So we need to get the number of ticks passed for the current timeslice, otherwise
    // we will return the same value each time a thread calls this function, which we do
    // not want. However, what we should do here to avoid any data-races with switches
    // we should disable irqs while reading this
    irqstate_t intStatus   = InterruptDisable();
    uint32_t    tick        = ApicReadLocal(APIC_CURRENT_COUNT);
    uint32_t    ticksPassed = ApicReadLocal(APIC_INITIAL_COUNT) - tick;
    _CRT_UNUSED(context);

    count->QuadPart = g_lapicTimer.Tick + ticksPassed;
    InterruptRestoreState(intStatus);
}

static void
ApicTimerGetFrequency(
        _In_ void*            context,
        _In_ UInteger64_t* frequency)
{
    _CRT_UNUSED(context);
    frequency->QuadPart = g_lapicTimer.Frequency;
}

static void
__Sleep(void)
{
    UInteger64_t tick, frequency, tickEnd;

    SystemTimerGetClockFrequency(&frequency);
    SystemTimerGetClockTick(&tick);

    // Run for 100ms, so divide the frequency (ticks per second) by 10
    tickEnd.QuadPart = tick.QuadPart + (frequency.QuadPart / 10);
    do {
        SystemTimerGetClockTick(&tick);
    } while (tick.QuadPart < tickEnd.QuadPart);
}

static void
ApicTimerRecalibrate(
        _In_ void* context)
{
    clock_t  lapicTicks;
    _CRT_UNUSED(context);

    // Setup initial local apic timer registers
    // TODO handle the case where the apic timer ticks WAY to quick for the 1 divider
    ApicWriteLocal(APIC_TIMER_VECTOR,    INTERRUPT_LAPIC);
    ApicWriteLocal(APIC_DIVIDE_REGISTER, APIC_TIMER_DIVIDER_1);
    ApicWriteLocal(APIC_INITIAL_COUNT,   0xFFFFFFFF); // Set counter to max, it counts down

    // Sleep for 100 ms
    __Sleep();

    // Stop counter and calibrate the frequency of the lapic
    lapicTicks = (0xFFFFFFFFU - ApicReadLocal(APIC_CURRENT_COUNT));
    assert(lapicTicks != 0xFFFFFFFF);
    ApicWriteLocal(APIC_TIMER_VECTOR, APIC_MASKED);

    lapicTicks *= 10; // ticks per second

    // Reset the timer data
    g_lapicTimer.Tick = 0;
    g_lapicTimer.Frequency = lapicTicks;

    g_lapicTimer.QuantumUnit = 1; // assume ns accuracy
    g_lapicTimer.Quantum     = lapicTicks / NSEC_PER_SEC; // number of ticks per ns
    if (lapicTicks < NSEC_PER_SEC) {
        g_lapicTimer.QuantumUnit = NSEC_PER_USEC; // we are using usec precision
        g_lapicTimer.Quantum     = lapicTicks / USEC_PER_SEC;
        if (lapicTicks < USEC_PER_SEC) {
            g_lapicTimer.QuantumUnit = NSEC_PER_MSEC; // msec precision
            g_lapicTimer.Quantum     = lapicTicks / MSEC_PER_SEC;
        }
    }

    // calculate the quantum
    TRACE("ApicTimerRecalibrate BusSpeed: %" PRIuIN " Hz", lapicTicks);
    TRACE("ApicTimerRecalibrate Quantum: %" PRIuIN ", QuantumUnit %u",
          g_lapicTimer.Quantum, g_lapicTimer.QuantumUnit);
}
