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
    bool     Available;
    bool     Enabled;
    bool     CalibrationMode;
    int      InterruptLine;
    uuid_t   Irq;
    uint32_t Frequency;
    uint64_t Tick;
} Pit_t;

// import the calibration ticker
extern uint32_t g_calibrationTick;

static oserr_t PITEnable(void*, bool enable);
static oserr_t PITConfigure(void*, UInteger64_t* frequency);
static void    PITGetFrequencyRange(void*, UInteger64_t* low, UInteger64_t* high);
static void    PITGetFrequency(void*, UInteger64_t*);
static void    PITGetCount(void*, UInteger64_t*);

static SystemTimerOperations_t g_pitOperations = {
        .Enable = PITEnable,
        .Configure = PITConfigure,
        .Recalibrate = NULL,
        .GetFrequencyRange = PITGetFrequencyRange,
        .GetFrequency = PITGetFrequency,
        .Read = PITGetCount,
};
static Pit_t g_pit = { 0 };

irqstatus_t
PitInterrupt(
        _In_ InterruptFunctionTable_t* NotUsed,
        _In_ void*                     Context)
{
    _CRT_UNUSED(NotUsed);
	_CRT_UNUSED(Context);

    // Are we in calibration mode?
    if (g_pit.CalibrationMode) {
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
__ConfigurePIT(
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
    oserr_t oserr;

    TRACE("PitInitialize(rtcAvailable=%i)", rtcAvailable);
    if (rtcAvailable) {
        TRACE("PitInitialize PIT will be disabled as the RTC is enabled");
        return OS_EOK;
    }

    // Mark PIT as available, which means we enable the use of calibration mode
    g_pit.Available = true;

    // Initialize the PIT to a frequency of 1000hz during boot so that the rest of the system
    // can calibrate its timers. When calibration is over, we disable the PIT and let the kernel
    // timer system deterimine whether it wants to reenable us.
    PitSetCalibrationMode(1);

    // Register us as a system timer, but only if we are not emulated by the HPET, otherwise
    // the system is better off using the HPET
    if (!HPETIsEmulatingLegacyController()) {
        oserr = SystemTimerRegister(
                "x86-pit",
                &g_pitOperations,
                SystemTimeAttributes_IRQ,
                &g_pit
        );
        if (oserr != OS_EOK) {
            WARNING("PitInitialize failed to register the platform timer");
        }
    }
	return OS_EOK;
}

void
PitSetCalibrationMode(
        _In_ int enable)
{
    TRACE("PitSetCalibrationMode(enable=%i)", enable);
    if (!g_pit.Available) {
        return;
    }

    if (enable) {
        PITConfigure(&g_pit, &(UInteger64_t) { .QuadPart = 1000 });
    }
    PITEnable(&g_pit, enable);
}

static oserr_t
PITEnable(
        _In_ void* context,
        _In_ bool  enable)
{
    Pit_t* pit = context;

    if (enable) {
        DeviceInterrupt_t deviceInterrupt = { { 0 } };
        if (pit->Enabled) {
            return OS_EOK;
        }

        // Install the PIT interrupt line, we cannot enable/disable the PIT in
        // other ways if the device itself is actually present, so the only way
        // is to mask/unmask irq lines
        deviceInterrupt.Line                  = PIT_IRQ;
        deviceInterrupt.Pin                   = INTERRUPT_NONE;
        deviceInterrupt.Vectors[0]            = INTERRUPT_NONE;
        deviceInterrupt.ResourceTable.Handler = PitInterrupt;
        deviceInterrupt.Context               = &g_pit;
        pit->Irq = InterruptRegister(
                &deviceInterrupt,
                INTERRUPT_EXCLUSIVE | INTERRUPT_KERNEL
        );
        if (pit->Irq == UUID_INVALID) {
            ERROR("PITEnable failed to register interrupt");
            return OS_EUNKNOWN;
        }

        // Store updated interrupt line
        pit->InterruptLine = deviceInterrupt.Line;
        pit->Enabled = true;
    } else {
        oserr_t oserr;

        if (!pit->Enabled) {
            return OS_EOK;
        }

        if (HPETIsEmulatingLegacyController()) {
            oserr = HPETComparatorStop(0);
            if (oserr != OS_EOK) {
                WARNING("PITEnable failed to stop hpet comparator.");
            }
        }

        oserr = InterruptUnregister(pit->Irq);
        if (oserr != OS_EOK) {
            WARNING("PITEnable failed to mask the PIT irq");
            return oserr;
        }
        pit->Irq = UUID_INVALID;
        pit->Enabled = false;
    }
    return OS_EOK;
}

static oserr_t
PITConfigure(
        _In_ void*         context,
        _In_ UInteger64_t* frequency)
{
    Pit_t* pit = context;

    // sanitize the frequency request, we don't want to do any invalid
    // divisions
    if (frequency->u.LowPart > PIT_FREQUENCY_HIGHEST ||
        frequency->u.LowPart < PIT_FREQUENCY_LOWEST) {
        return OS_EINVALPARAMS;
    }

    if (HPETIsEmulatingLegacyController()) {
        // Counter 0 is the IRQ 0 emulator
        HPETComparatorStart(0, frequency->u.LowPart, 1, pit->InterruptLine);
    } else {
        uint16_t divisor = PIT_FREQUENCY / frequency->u.LowPart;
        __ConfigurePIT(divisor);
    }
    pit->Frequency = frequency->u.LowPart;
    return OS_EOK;
}

static void
PITGetFrequencyRange(
        _In_ void*         context,
        _In_ UInteger64_t* low,
        _In_ UInteger64_t* high)
{
    _CRT_UNUSED(context);
    low->QuadPart = PIT_FREQUENCY_LOWEST;
    high->QuadPart = PIT_FREQUENCY_HIGHEST;
}

static void
PITGetFrequency(
        _In_ void*         context,
        _In_ UInteger64_t* frequency)
{
    Pit_t* pit = context;
    frequency->QuadPart = pit->Frequency;
}

static void
PITGetCount(
        _In_ void*         context,
        _In_ UInteger64_t* tick)
{
    Pit_t* pit = context;
    tick->QuadPart = pit->Tick;
}
