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
    int      Enabled;
    int      InterruptLine;
    uuid_t   Irq;
    uint64_t Frequency;
    uint64_t Tick;
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

static uint16_t
__ReadCurrentTick(void)
{
    uint8_t low, high;

    // Select the first channel
    WriteDirectIo(
            DeviceIoPortBased,
            PIT_IO_BASE + PIT_REGISTER_COMMAND, 1,
            PIT_COMMAND_COUNTER_0
    );

    // Read the current count
    ReadDirectIo(
            DeviceIoPortBased,
            PIT_IO_BASE + PIT_REGISTER_COUNTER0, 1,
            (size_t*)&low
    );
    ReadDirectIo(
            DeviceIoPortBased,
            PIT_IO_BASE + PIT_REGISTER_COUNTER0, 1,
            (size_t*)&high
    );
    return ((uint16_t)high << 8) | low;
}

oserr_t
PitInitialize(
        _In_ int rtcAvailable)
{
	DeviceInterrupt_t deviceInterrupt = { { 0 } };
    oserr_t           oserr;

    TRACE("PitInitialize(rtcAvailable=%i)", rtcAvailable);
    if (rtcAvailable) {
        TRACE("PitInitialize PIT will be disabled as the RTC is enabled");
        return OS_EOK;
    }

    // Otherwise, we set the PIT as enabled
    g_pit.Enabled = 1;

    // Initialize the device interrupt
    deviceInterrupt.Line                  = PIT_IRQ;
    deviceInterrupt.Pin                   = INTERRUPT_NONE;
    deviceInterrupt.Vectors[0]            = INTERRUPT_NONE;
    deviceInterrupt.ResourceTable.Handler = PitInterrupt;
    deviceInterrupt.Context               = &g_pit;
    g_pit.Irq = InterruptRegister(&deviceInterrupt, INTERRUPT_EXCLUSIVE | INTERRUPT_KERNEL);
    if (g_pit.Irq == UUID_INVALID) {
        ERROR("PitInitialize Failed to register interrupt");
        return OS_EUNKNOWN;
    }

    // Store updated interrupt line
    g_pit.InterruptLine = deviceInterrupt.Line;

    // Initialize the PIT to a frequency of 1000hz during boot so that the rest of the system
    // can calibrate its timers. When calibration is over, we then reinitialize the PIT to the lowest
    // frequency since we do not use the PIT for any high precision, but instead only use it for
    // time-keeping if the RTC is not available.
    PitSetCalibrationMode(1);

    // Register us as a system timer
    oserr = SystemTimerRegister(
            &g_pitOperations,
            SystemTimeAttributes_IRQ,
            irq,
            &g_pit
    );
    if (oserr != OS_EOK) {
        WARNING("PitInitialize failed to register the platform timer");
    }
	return OS_EOK;
}

void
PitSetCalibrationMode(
        _In_ int enable)
{
    uint16_t divisor = 0;
    oserr_t  oserr;

    TRACE("PitSetCalibrationMode(enable=%i)", enable);
    if (!g_pit.Enabled) {
        return;
    }

    if (!enable) {
        // After calibration period, we will stop using the PIT timer if
        // the HPET is present. The PIT has a way to large overhead when reading
        // the counter values, which is neccessary if we want better precision. We should
        // in case that the HPET is not present, increase the rate of fire for the PIT to
        // 1000hz and stick to 1ms accuracy. This is an TODO
        if (HPETIsPresent()) {
            if (HPETIsEmulatingLegacyController()) {
                oserr = HPETComparatorStop(0);
                if (oserr != OS_EOK) {
                    WARNING("PitSetCalibrationMode failed to stop hpet comparator.");
                }
            } else {
                oserr = InterruptUnregister(g_pit.Irq);
                if (oserr != OS_EOK) {
                    WARNING("PitSetCalibrationMode failed to mask the PIT irq");
                }
                g_pit.Irq = UUID_INVALID;
            }
            return;
        }

        // Otherwise, the HPET is not present, then we must use the PIT.
    }

    // In both calibration mode and non-calibration mode we would like to stick
    // to a 1ms accuracy. If we really need to fall back to the PIT, that means we
    // have no better means of measuring accuracy.
    if (enable) {
        g_pit.Frequency = 1000;
        divisor         = PIT_FREQUENCY / 1000;
    } else {
        // Initialize the PIT to the lowest frequency possible, this is determined by whether
        // we are emulated by the HPET or this is a native PIT chip. The lowest we can go in
        // the native PIT, is 18.2065Hz.
        if (HPETIsEmulatingLegacyController()) {
            g_pit.Frequency = 1000;
        } else {
            g_pit.Frequency = 18;
            divisor         = 0;
        }
    }

    if (HPETIsEmulatingLegacyController()) {
        // Counter 0 is the IRQ 0 emulator
        HPETComparatorStart(0, g_pit.Frequency, 1, g_pit.InterruptLine);
    } else {
        __StartPit(divisor);
    }
}

static void
PitGetCount(
        _In_ void*         context,
        _In_ UInteger64_t* tick)
{
    Pit_t*   pit          = context;
    uint16_t currentValue = __ReadCurrentTick();

    // So we need to do two things here, we both need to read the tick part,
    // but also the current value of the PIT to accurately calculate the current
    // time into the current tick. Each tick is 55ms (54.93ms)
    tick->QuadPart = (pit->Tick * 55) * NSEC_PER_MSEC;
    tick->QuadPart += (((currentValue * 100) / 0xFFFF) * 55) / 100;
}

static void
PitGetFrequency(
        _In_ void*         context,
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
