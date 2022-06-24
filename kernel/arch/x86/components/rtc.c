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
 * X86 CMOS & RTC (Clock) Driver
 * http://wiki.osdev.org/RTC
 */

#define __TRACE

#include <debug.h>
#include <ddk/io.h>
#include <hpet.h>
#include <interrupts.h>
#include <machine.h>
#include <arch/x86/cmos.h>

// import the calibration ticker as we use it during boot
extern uint32_t g_calibrationTick;
extern Cmos_t   g_cmos;

InterruptStatus_t
RtcInterrupt(
        _In_ InterruptFunctionTable_t*  NotUsed,
        _In_ void*                      Context)
{
    uint8_t status;

    _CRT_UNUSED(NotUsed);
    _CRT_UNUSED(Context);

    // use the result of the status register to determine mode
    status = CmosRead(CMOS_REGISTER_STATUS_C);
    if (status & CMOSC_IRQ_PERIODIC) {
        // this was a calibration irq
        uint32_t tick = READ_VOLATILE(g_calibrationTick);
        WRITE_VOLATILE(g_calibrationTick, tick + 1);
        return InterruptHandled;
    }

    // Should we ever try to resync the time at specific intervals?
    if (GetMachine()->SystemTimers.WallClock.Year == 0) {
        CmosReadSystemTime(&GetMachine()->SystemTimers.WallClock);
    }
    else {
        // Update system time with 1 second
        SystemTimerWallClockAddTime(1);
    }
    return InterruptHandled;
}

InterruptStatus_t
RtcHpetInterrupt(
        _In_ InterruptFunctionTable_t*  NotUsed,
        _In_ void*                      Context)
{
    _CRT_UNUSED(NotUsed);
    _CRT_UNUSED(Context);

    if (g_cmos.CalibrationMode) {
        // this was a calibration irq
        uint32_t tick = READ_VOLATILE(g_calibrationTick);
        WRITE_VOLATILE(g_calibrationTick, tick + 1);
        return InterruptHandled;
    }

    // Should we ever try to resync the time at specific intervals?
    if (GetMachine()->SystemTimers.WallClock.Year == 0) {
        CmosReadSystemTime(&GetMachine()->SystemTimers.WallClock);
    }
    else {
        // Update system time with 1 second
        SystemTimerWallClockAddTime(1);
    }
    return InterruptHandled;
}

static uint8_t
__DisableRtc(void)
{
    uint8_t statusB;

    statusB = CmosRead(CMOS_REGISTER_STATUS_B);
    statusB &= ~(CMOSB_IRQ_PERIODIC | CMOSB_IRQ_ALARM | CMOSB_IRQ_UPDATE | CMOSB_IRQ_SQWAVFRQ);
    CmosWrite(CMOS_REGISTER_STATUS_B, statusB);
    return CmosRead(CMOS_REGISTER_STATUS_B);
}

OsStatus_t
RtcInitialize(
    _In_ Cmos_t* cmos)
{
    DeviceInterrupt_t deviceInterrupt = {{0 } };
    uint8_t           rate = 0x06; // must be between 3 and 15
    TRACE("RtcInitialize()");

    // disable the RTC for starters
    (void)__DisableRtc();

    // write default tick rate
    CmosWrite(CMOS_REGISTER_STATUS_A, CMOSA_DIVIDER_32KHZ | rate);

    // initialize the device interrupt
    deviceInterrupt.Line                  = CMOS_RTC_IRQ;
    deviceInterrupt.Pin                   = INTERRUPT_NONE;
    deviceInterrupt.Vectors[0]            = INTERRUPT_NONE;
    deviceInterrupt.ResourceTable.Handler = (HpetIsEmulatingLegacyController() == OsOK ? RtcHpetInterrupt : RtcInterrupt);
    deviceInterrupt.Context               = cmos;

    cmos->Irq = InterruptRegister(
            &deviceInterrupt,
            INTERRUPT_EXCLUSIVE | INTERRUPT_KERNEL
    );
    if (cmos->Irq == UUID_INVALID) {
        ERROR("RtcInitialize failed to register interrupt for rtc");
        return OsError;
    }

    // Store the interrupt line for the RTC
    g_cmos.RtcLine = deviceInterrupt.Line;

    // Set calibration mode
    RtcSetCalibrationMode(1);

    return OsOK;
}

void
RtcSetCalibrationMode(
        _In_ int enable)
{
    uint8_t statusB;

    if (!g_cmos.RtcAvailable) {
        return;
    }

    g_cmos.CalibrationMode = enable;

    // In calibration mode, we request a frequency of 1000hz, in default mode
    // we just want an interrupt once every second
    if (HpetIsEmulatingLegacyController() == OsOK) {
        uint64_t frequency = MAX(1, 1000 * enable);
        if (!enable) {
            // very naive attempt at syncing the comparator with the CMOS clock update
            CmosWaitForUpdate();
        }
        HpetComparatorStart(1, frequency, 1, g_cmos.RtcLine);
        return;
    }

    statusB = __DisableRtc();
    CmosRead(CMOS_REGISTER_STATUS_C);
    if (enable) {
        statusB |= CMOSB_IRQ_UPDATE | CMOSB_IRQ_PERIODIC | CMOSB_IRQ_SQWAVFRQ;
    }
    else {
        statusB |= CMOSB_IRQ_UPDATE;
    }
    CmosWrite(CMOS_REGISTER_STATUS_B, statusB);
}
