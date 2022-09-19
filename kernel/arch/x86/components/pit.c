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
 * X86 PIT (Timer) Driver
 * http://wiki.osdev.org/PIT
 */

#define __MODULE "PIT0"
#define __TRACE

#include <arch/io.h>
#include <component/timer.h>
#include <ddk/io.h>
#include <debug.h>
#include <hpet.h>
#include <interrupts.h>
#include <arch/x86/pit.h>

typedef struct Pit {
    int     Enabled;
    int     InterruptLine;
    tick_t  Frequency;
    tick_t  Tick;
    int16_t Drift;
    int16_t DriftAccumulated;
} Pit_t;

// import the calibration ticker
extern uint32_t g_calibrationTick;

static void PitGetCount(void*, UInteger64_t*);
static void PitGetFrequency(void*, UInteger64_t*);
static void PitNoOperation(void*);

static SystemTimerOperations_t g_pitOperations = {
        .Read = PitGetCount,
        .GetFrequency = PitGetFrequency,
        .Recalibrate = PitNoOperation
};
static Pit_t g_pit = { 0 };

irqstatus_t
PitInterrupt(
        _In_ InterruptFunctionTable_t*  NotUsed,
        _In_ void*                      Context)
{
    _CRT_UNUSED(NotUsed);
	_CRT_UNUSED(Context);

    // Are we in calibration mode?
    if (g_pit.Frequency == 1000) {
        uint32_t ticker = READ_VOLATILE(g_calibrationTick);
        WRITE_VOLATILE(g_calibrationTick, ticker + 1);
        return IRQSTATUS_HANDLED;
    }

    // Otherwise, we act as the wall-clock counter. Now we are configured to run at lowest
    // possible frequency, which is 18.2065Hz. This means we tick each 54.93ms, which will result
    // in a drift of -11ms each second. That means every 91 interrupt we will skip adding a second
    g_pit.Tick++;
    g_pit.DriftAccumulated += g_pit.Drift;
    if (!(g_pit.Tick % g_pit.Frequency)) {
        int seconds = 1;

        // drift is accumulated in MS for the PIT
        if (g_pit.DriftAccumulated >= 1000) {
            g_pit.DriftAccumulated -= 1000;
            seconds++;
        }
        else if (g_pit.DriftAccumulated <= -1000) {
            g_pit.DriftAccumulated += 1000;
            seconds--;
        }
    }
	return IRQSTATUS_HANDLED;
}

static void
__StartPit(
        _In_ uint16_t divisor)
{
    // We use counter 0, select counter 0 and configure it
    WriteDirectIo(DeviceIoPortBased, PIT_IO_BASE + PIT_REGISTER_COMMAND, 1,
                  PIT_COMMAND_MODE3 | PIT_COMMAND_FULL | PIT_COMMAND_COUNTER_0);

    // Write divisor to the PIT chip
    WriteDirectIo(DeviceIoPortBased, PIT_IO_BASE + PIT_REGISTER_COUNTER0, 1, divisor & 0xFF);
    WriteDirectIo(DeviceIoPortBased, PIT_IO_BASE + PIT_REGISTER_COUNTER0, 1, (divisor >> 8) & 0xFF);
}

oserr_t
PitInitialize(
        _In_ int rtcAvailable)
{
	DeviceInterrupt_t deviceInterrupt = {{0 } };
    uuid_t            irq;
    oserr_t        osStatus;

    TRACE("PitInitialize(rtcAvailable=%i)", rtcAvailable);
    if (rtcAvailable) {
        TRACE("PitInitialize PIT will be disabled as the RTC is enabled");
        return OsOK;
    }

    // Otherwise, we set the PIT as enabled
    g_pit.Enabled = 1;

    // Initialize the device interrupt
    deviceInterrupt.Line                  = PIT_IRQ;
    deviceInterrupt.Pin                   = INTERRUPT_NONE;
    deviceInterrupt.Vectors[0]            = INTERRUPT_NONE;
    deviceInterrupt.ResourceTable.Handler = PitInterrupt;
    deviceInterrupt.Context               = &g_pit;
    irq = InterruptRegister(&deviceInterrupt, INTERRUPT_EXCLUSIVE | INTERRUPT_KERNEL);
    if (irq == UUID_INVALID) {
        ERROR("Failed to register interrupt");
        return OsError;
    }

    // Store updated interrupt line
    g_pit.InterruptLine = deviceInterrupt.Line;

    // Initialize the PIT to a frequency of 1000hz during boot so that the rest of the system
    // can calibrate its timers. When calibration is over, we then reinitialize the PIT to the lowest
    // frequency since we do not use the PIT for any high precision, but instead only use it for
    // time-keeping if the RTC is not available.
    PitSetCalibrationMode(1);

    // Register us as a system timer
    osStatus = SystemTimerRegister(
            &g_pitOperations,
            SystemTimeAttributes_IRQ,
            irq,
            &g_pit);
    if (osStatus != OsOK) {
        WARNING("PitInitialize failed to register the platform timer");
    }
	return OsOK;
}

void
PitSetCalibrationMode(
        _In_ int enable)
{
    uint16_t divisor = 0;

    TRACE("PitSetCalibrationMode(enable=%i)", enable);
    if (!g_pit.Enabled) {
        return;
    }

    g_pit.DriftAccumulated = 0;
    if (enable) {
        g_pit.Frequency = 1000;
        g_pit.Drift     = 0; // there is drift, but we don't care in calibration mode
        divisor         = PIT_FREQUENCY / 1000;
    }
    else {
        // Initialize the PIT to the lowest frequency possible, this is determined by whether
        // we are emulated by the HPET or this is a native PIT chip. The lowest we can go in
        // the native PIT, is 18.2065Hz.
        if (HpetIsEmulatingLegacyController() == OsOK) {
            g_pit.Frequency = 1;
            g_pit.Drift     = 0;
        }
        else {
            g_pit.Frequency = 18;
            g_pit.Drift     = -11;
            divisor         = 0xFFFF;
        }
    }

    if (HpetIsEmulatingLegacyController() == OsOK) {
        // Counter 0 is the IRQ 0 emulator
        HpetComparatorStart(0, g_pit.Frequency, 1, g_pit.InterruptLine);
    }
    else {
        __StartPit(divisor);
    }
}

static void
PitGetCount(
        _In_ void*            context,
        _In_ UInteger64_t* tick)
{
    Pit_t* pit = context;
    tick->QuadPart = pit->Tick;
}

static void
PitGetFrequency(
        _In_ void*            context,
        _In_ UInteger64_t* frequency)
{
    Pit_t* pit = context;
    frequency->QuadPart = pit->Frequency;
}

static void
PitNoOperation(
        _In_ void* context)
{
    // no-op
    _CRT_UNUSED(context);
}
