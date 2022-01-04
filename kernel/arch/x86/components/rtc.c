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
#include <timers.h>
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
    if (GetMachine()->SystemTime.Year == 0) {
        CmosReadSystemTime(&GetMachine()->SystemTime);
    }
    else {
        // Update system time with 1 second
        TimeWallClockAddTime(1);
    }
    return InterruptHandled;
}

OsStatus_t
RtcInitialize(
    _In_ Cmos_t* cmos)
{
    DeviceInterrupt_t deviceInterrupt = {{0 } };
    uint8_t           statusB;
    uint8_t           rate = 0x06; // must be between 3 and 15
    TRACE("RtcInitialize()");

    // clear all irqs enabled from the RTC
    statusB = CmosRead(CMOS_REGISTER_STATUS_B);
    statusB &= ~(CMOSB_IRQ_PERIODIC | CMOSB_IRQ_ALARM | CMOSB_IRQ_UPDATE | CMOSB_IRQ_SQWAVFRQ);
    CmosWrite(CMOS_REGISTER_STATUS_B, statusB);
    (void)CmosRead(CMOS_REGISTER_STATUS_B);

    CmosWrite(CMOS_REGISTER_STATUS_A, CMOSA_DIVIDER_32KHZ | rate);

    // initialize the device interrupt
    deviceInterrupt.Line                  = CMOS_RTC_IRQ;
    deviceInterrupt.Pin                   = INTERRUPT_NONE;
    deviceInterrupt.Vectors[0]            = INTERRUPT_NONE;
    deviceInterrupt.ResourceTable.Handler = RtcInterrupt;
    deviceInterrupt.Context               = cmos;

    cmos->Irq = InterruptRegister(&deviceInterrupt, INTERRUPT_EXCLUSIVE | INTERRUPT_KERNEL);
    if (cmos->Irq == UUID_INVALID) {
        ERROR("RtcInitialize failed to register interrupt for rtc");
        return OsError;
    }

    // Read the status register to make sure any ints are acknowledged before
    // starting usage
    CmosRead(CMOS_REGISTER_STATUS_C);

    // Detect whether we are emulated by the hpet
    if (HpetIsEmulatingLegacyController() == OsSuccess) {
        TRACE("RtcInitialize RTC is emulated, syncing CMOS time");
        // If we are emulated then we sync the clock immediately and start a timer that should fire
        // once every second
        CmosWaitForUpdate();
        CmosReadSystemTime(&GetMachine()->SystemTime);

    	// Counter 1 is the IRQ 8 emulator
        HpComparatorStart(1, 1, 1, deviceInterrupt.Line);
    }
    else {
        // start the RTC update and frequency timer
        statusB = CmosRead(CMOS_REGISTER_STATUS_B);
        statusB |= CMOSB_IRQ_UPDATE | CMOSB_IRQ_PERIODIC | CMOSB_IRQ_SQWAVFRQ;
        CmosWrite(CMOS_REGISTER_STATUS_B, statusB);
    }

    // Clear pending interrupt again
    CmosRead(CMOS_REGISTER_STATUS_C);
    return OsSuccess;
}

void
RtcSetCalibrationMode(
        _In_ int enable)
{
    uint8_t statusB;

    if (!g_cmos.RtcAvailable) {
        return;
    }

    // disable interrupts
    statusB = CmosRead(CMOS_REGISTER_STATUS_B);
    statusB &= ~(CMOSB_IRQ_PERIODIC | CMOSB_IRQ_ALARM | CMOSB_IRQ_UPDATE | CMOSB_IRQ_SQWAVFRQ);
    CmosWrite(CMOS_REGISTER_STATUS_B, statusB);

    statusB = CmosRead(CMOS_REGISTER_STATUS_B);
    if (enable) {
        statusB |= CMOSB_IRQ_UPDATE | CMOSB_IRQ_PERIODIC | CMOSB_IRQ_SQWAVFRQ;
    }
    else {
        statusB |= CMOSB_IRQ_UPDATE;
    }
    CmosWrite(CMOS_REGISTER_STATUS_B, statusB);
}
