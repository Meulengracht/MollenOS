/**
 * Copyright, Philip Meulengracht
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
 */

#include <arch/utils.h>
#include <component/timer.h>
#include <interrupts.h>
#include <machine.h>
#include <threading.h>
#include "private.h"

extern void Arm64EnableTimerInterrupt(void);

static uint64_t
__ReadCounter(void)
{
    uint64_t ticks;

    // Order the physical-counter sample after earlier instructions on this CPU.
    __asm__ volatile("isb\nmrs %0, cntpct_el0" : "=r"(ticks));
    return ticks;
}

static void
__Read(
    _In_  void*         context,
    _Out_ UInteger64_t* value)
{
    (void)context;
    value->QuadPart = __ReadCounter();
}

static void
__Frequency(
    _In_  void*         context,
    _Out_ UInteger64_t* value)
{
    (void)context;
    value->QuadPart = GetMachine()->Processor.PlatformData.CounterFrequency;
}

static SystemTimerOperations_t g_timerOperations = {
    .Read = __Read,
    .GetFrequency = __Frequency
};

static void
__ArmTimer(
    _In_ uint64_t deadline)
{
    // CNTP_CVAL is an absolute physical-counter deadline. Enable the timer
    // without masking its output, then synchronize before returning to a thread.
    __asm__ volatile(
        "msr cntp_cval_el0, %0\n"
        "msr cntp_ctl_el0, %1\n"
        "isb" :: "r"(deadline), "r"(ARM64_TIMER_ENABLE) : "memory");
}

void
Arm64TimerAdvance(
    _In_ int preemptive)
{
    struct Arm64CpuLocal* local = &g_arm64CpuLocals[ArchGetProcessorCoreId()];

    uint64_t frequency = GetMachine()->Processor.PlatformData.CounterFrequency;
    uint64_t now = __ReadCounter();
    uint64_t elapsed = now - local->LastTimerTick;
    uint64_t nanoseconds;
    uint64_t deadline;
    clock_t  next = ARM64_TIMER_DEFAULT_NS;

    local->LastTimerTick = now;
    
    // Split seconds and fractional ticks rather than multiplying all elapsed
    // ticks at once. The next deadline rounds up so it never fires early.
    nanoseconds = (elapsed / frequency) * ARM64_NANOSECONDS_PER_SECOND +
        ((elapsed % frequency) * ARM64_NANOSECONDS_PER_SECOND) / frequency;
    
    ThreadingAdvance(preemptive, nanoseconds, &next);
    if (!next) {
        // No runnable object or sleep deadline remains. A rescheduling SGI
        // will rearm the timer; avoid an interrupt storm on an idle core.
        __asm__ volatile("msr cntp_ctl_el0, xzr\nisb" ::: "memory");
        return;
    }
    
    deadline = __ReadCounter() + ((uint64_t)next / ARM64_NANOSECONDS_PER_SECOND) * frequency +
        (((uint64_t)next % ARM64_NANOSECONDS_PER_SECOND) * frequency +
            ARM64_NANOSECONDS_PER_SECOND - 1) / ARM64_NANOSECONDS_PER_SECOND;
    __ArmTimer(deadline);
}

static irqstatus_t
__TimerInterrupt(
    _In_ InterruptFunctionTable_t* functions,
    _In_ void*                     context)
{
    // We are not using the functions and context parameters.
    (void)functions;
    (void)context;

    // Advance the timer and handle any pending timer events.
    Arm64TimerAdvance(1);
    return IRQSTATUS_HANDLED;
}

void
Arm64TimerInitializeCore(void)
{
    uint64_t now = __ReadCounter();
    uint64_t frequency = GetMachine()->Processor.PlatformData.CounterFrequency;
    uint64_t deadline = now + frequency / ARM64_TIMER_INITIAL_HZ;

    g_arm64CpuLocals[ArchGetProcessorCoreId()].LastTimerTick = now;
    
    Arm64EnableTimerInterrupt();
    __ArmTimer(deadline);
}

oserr_t
PlatformTimersInitialize(void)
{
    DeviceInterrupt_t interrupt = { 0 };
    oserr_t           status;
    uuid_t            id;

    if (!GetMachine()->Processor.PlatformData.CounterFrequency) {
        return OS_EINVALPARAMS;
    }
    
    interrupt.Line = g_arm64Platform.TimerInterrupt;
    interrupt.Pin = INTERRUPT_NONE;
    interrupt.Vectors[0] = INTERRUPT_NONE;
    interrupt.AcpiConform = INTERRUPT_ACPICONFORM_PRESENT | INTERRUPT_ACPICONFORM_TRIGGERMODE;
    interrupt.ResourceTable.Handler = __TimerInterrupt;
    
    id = InterruptRegister(&interrupt, INTERRUPT_KERNEL | INTERRUPT_EXCLUSIVE);
    if (id == UUID_INVALID) {
        return OS_EUNKNOWN;
    }
    
    status = SystemTimerRegister(
        "arm main counter",
        &g_timerOperations,
        SystemTimeAttributes_COUNTER | SystemTimeAttributes_CALIBRATED | SystemTimeAttributes_HPC,
        NULL
    );
    if (status != OS_EOK) {
        return status;
    }
    
    Arm64TimerInitializeCore();
    return OS_EOK;
}
