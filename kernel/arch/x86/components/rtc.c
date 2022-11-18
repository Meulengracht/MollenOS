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

#define __need_minmax
#include <debug.h>
#include <ddk/io.h>
#include <hpet.h>
#include <interrupts.h>
#include <machine.h>
#include <arch/x86/cmos.h>

// import the calibration ticker as we use it during boot
extern uint32_t g_calibrationTick;
extern Cmos_t   g_cmos;

static oserr_t RTCEnable(void*, bool enable);
static oserr_t RTCConfigure(void*, UInteger64_t* frequency);
static void    RTCGetFrequencyRange(void*, UInteger64_t* low, UInteger64_t* high);
static void    RTCGetFrequency(void*, UInteger64_t*);
static void    RTCGetCount(void*, UInteger64_t*);

static SystemTimerOperations_t g_rtcOperations = {
        .Enable = RTCEnable,
        .Configure = RTCConfigure,
        .Recalibrate = NULL,
        .GetFrequencyRange = RTCGetFrequencyRange,
        .GetFrequency = RTCGetFrequency,
        .Read = RTCGetCount,
};

irqstatus_t
RtcInterrupt(
        _In_ InterruptFunctionTable_t*  NotUsed,
        _In_ void*                      Context)
{
    uint8_t irqstat;

    _CRT_UNUSED(NotUsed);
    _CRT_UNUSED(Context);

    // use the result of the status register to determine mode
    irqstat = CmosRead(CMOS_REGISTER_STATUS_B);
    if (irqstat & CMOSC_IRQ_PERIODIC) {
        if (g_cmos.CalibrationMode) {
            uint32_t tick = READ_VOLATILE(g_calibrationTick);
            WRITE_VOLATILE(g_calibrationTick, tick + 1);
        } else {
            g_cmos.Ticks++;
        }
    }
    (void)CmosRead(CMOS_REGISTER_STATUS_C);
    return IRQSTATUS_HANDLED;
}

static void
__DisableRtc(
        _In_ Cmos_t* cmos)
{
    uint8_t statusB;

    statusB = CmosRead(CMOS_REGISTER_STATUS_B);
    statusB &= ~(CMOSB_IRQ_PERIODIC | CMOSB_IRQ_ALARM | CMOSB_IRQ_UPDATE | CMOSB_IRQ_SQWAVFRQ);
    CmosWrite(CMOS_REGISTER_STATUS_B, statusB);
    (void)CmosRead(CMOS_REGISTER_STATUS_B);

    if (HPETIsEmulatingLegacyController()) {
        HPETComparatorStop(1);
    }
    cmos->RtcEnabled = false;
}

static void
__EnableRTC(
        _In_ Cmos_t* cmos)
{
    uint8_t statusB;

    statusB = CmosRead(CMOS_REGISTER_STATUS_B);
    statusB |= CMOSB_IRQ_UPDATE | CMOSB_IRQ_PERIODIC | CMOSB_IRQ_SQWAVFRQ;

    // make sure the StatusC register is acked before we start it
    CmosRead(CMOS_REGISTER_STATUS_C);
    CmosWrite(CMOS_REGISTER_STATUS_B, statusB);
    (void)CmosRead(CMOS_REGISTER_STATUS_B);

    if (HPETIsEmulatingLegacyController()) {
        HPETComparatorStart(1, cmos->Frequency, 1, cmos->InterruptLine);
    }
    cmos->RtcEnabled = true;
}

static void
__ConfigureRTC(
        _In_ UInteger64_t* frequency,
        _In_ UInteger64_t* result)
{
    // initialize to highest rate value (lowest frequency)
    uint32_t current = 2;
    uint16_t rate    = 15;

    // Find nearest frequency that matches what we want
    while (rate > 3) {
        uint32_t distanceToCurrent = abs((int)(frequency->s.LowPart) - (int)current);
        uint32_t distanceToNext    = abs((int)(frequency->s.LowPart) - (int)(current << 2));
        if (distanceToCurrent < distanceToNext) {
            break;
        }
        current <<= 2;
        rate--;
    }

    // store resulting frequency
    result->QuadPart = current;

    // write the calculated rate
    CmosWrite(CMOS_REGISTER_STATUS_A, CMOSA_DIVIDER_32KHZ | rate);
}

static oserr_t
__InstallInterrupt(
        _In_ Cmos_t* cmos)
{
    DeviceInterrupt_t deviceInterrupt = { { 0 } };

    // initialize the device interrupt
    deviceInterrupt.Line                  = CMOS_RTC_IRQ;
    deviceInterrupt.Pin                   = INTERRUPT_NONE;
    deviceInterrupt.Vectors[0]            = INTERRUPT_NONE;
    deviceInterrupt.ResourceTable.Handler = RtcInterrupt;
    deviceInterrupt.Context               = cmos;

    cmos->Irq = InterruptRegister(
            &deviceInterrupt,
            INTERRUPT_EXCLUSIVE | INTERRUPT_KERNEL
    );
    if (cmos->Irq == UUID_INVALID) {
        ERROR("RtcInitialize failed to register interrupt for rtc");
        return OS_EUNKNOWN;
    }

    // Store the updated interrupt line for the RTC
    cmos->InterruptLine = deviceInterrupt.Line;
    return OS_EOK;
}

oserr_t
RtcInitialize(
    _In_ Cmos_t* cmos)
{
    oserr_t oserr;
    TRACE("RtcInitialize()");

    // disable the RTC for starters
    __DisableRtc(cmos);

    // install the interrupt
    oserr = __InstallInterrupt(cmos);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Initialize the RTC to a frequency of 1000hz during boot so that the rest of the system
    // can calibrate its timers. When calibration is over, we disable the RTC and let the kernel
    // timer system deterimine whether it wants to reenable us.
    RtcSetCalibrationMode(1);

    // Register us as a system timer, but only if we are not emulated by the HPET, otherwise
    // the system is better off using the HPET
    if (!HPETIsEmulatingLegacyController()) {
        oserr = SystemTimerRegister(
                "x86-rtc",
                &g_rtcOperations,
                SystemTimeAttributes_IRQ,
                &g_cmos
        );
        if (oserr != OS_EOK) {
            WARNING("PitInitialize failed to register the platform timer");
        }
    }
    return OS_EOK;
}

void
RtcSetCalibrationMode(
        _In_ int enable)
{
    if (!g_cmos.RtcAvailable) {
        return;
    }

    g_cmos.CalibrationMode = enable;

    if (enable) {
        RTCConfigure(&g_cmos, &(UInteger64_t) { .QuadPart = 1000 });
    }
    RTCEnable(&g_cmos, enable);
}

static oserr_t
RTCEnable(
        _In_ void* context,
        _In_ bool  enable)
{
    Cmos_t* cmos = context;

    if (enable) {
        if (cmos->RtcEnabled) {
            return OS_EOK;
        }
        __EnableRTC(cmos);
    } else {
        if (!cmos->RtcEnabled) {
            return OS_EOK;
        }
        __DisableRtc(cmos);
    }
    return OS_EOK;
}

static oserr_t
RTCConfigure(
        _In_ void*         context,
        _In_ UInteger64_t* frequency)
{
    Cmos_t* cmos = context;

    // sanitize the frequency request, we don't want to do any invalid
    // divisions
    if (frequency->u.LowPart > RTC_FREQUENCY_HIGHEST ||
        frequency->u.LowPart < RTC_FREQUENCY_LOWEST) {
        return OS_EINVALPARAMS;
    }

    if (HPETIsEmulatingLegacyController()) {
        // Counter 0 is the IRQ 0 emulator
        HPETComparatorStart(1, frequency->u.LowPart, 1, cmos->InterruptLine);
    } else {
        __ConfigureRTC(frequency, frequency);
    }
    cmos->Frequency = frequency->u.LowPart;
    return OS_EOK;
}

static void
RTCGetFrequencyRange(
        _In_ void*         context,
        _In_ UInteger64_t* low,
        _In_ UInteger64_t* high)
{
    _CRT_UNUSED(context);
    low->QuadPart  = RTC_FREQUENCY_LOWEST;
    high->QuadPart = RTC_FREQUENCY_HIGHEST;
}

static void
RTCGetFrequency(
        _In_ void*         context,
        _In_ UInteger64_t* frequency)
{
    Cmos_t* cmos = context;
    frequency->QuadPart = cmos->Frequency;
}

static void
RTCGetCount(
        _In_ void*         context,
        _In_ UInteger64_t* tick)
{
    Cmos_t* cmos = context;
    tick->QuadPart = cmos->Ticks;
}
