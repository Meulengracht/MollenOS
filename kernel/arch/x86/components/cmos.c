/**
 * Copyright 2011, Philip Meulengracht
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
 * http://wiki.osdev.org/CMOS#The_Real-Time_Clock
 */
#define __MODULE "CMOS"
#define __TRACE

#include <acpiinterface.h>
#include <arch/x86/cmos.h>
#include <arch/io.h>
#include <arch/interrupts.h>
#include <component/timer.h>
#include <debug.h>
#include <hpet.h>

static void __ReadSystemTime(void*, SystemTime_t*);
static void __WaitForUpdate(void* context);
static void __RequestUpdate(void* context);

Cmos_t g_cmos = { 0 };

oserr_t
CmosInitialize(
    _In_ int initializeRtc)
{
    SystemWallClockOperations_t ops = { NULL, NULL, NULL };
    oserr_t                     oserr;
    
    TRACE("CmosInitialize(rtc %" PRIiIN ")", initializeRtc);

    if (AcpiAvailable() == ACPI_AVAILABLE) {
        g_cmos.AcpiCentury = AcpiGbl_FADT.Century;
    }
    g_cmos.RtcAvailable = initializeRtc;

    // If the HPET is emulating the RTC then we cannot rely on correct clock
    // updates. Instead, do other kinds of clock syncs
    if (!HPETIsEmulatingLegacyController()) {
        ops.RequestSync = __RequestUpdate;
    }
    ops.PerformSync = __WaitForUpdate;
    ops.Read        = __ReadSystemTime;

    oserr = SystemWallClockRegister(&ops, &g_cmos);
    if (oserr != OS_EOK) {
        return oserr;
    }
    
    if (g_cmos.RtcAvailable) {
        return RtcInitialize(&g_cmos);
    }
    return OS_EOK;
}

uint8_t
CmosRead(
    _In_ uint8_t cmosRegister)
{
    size_t  cmosValue = 0;
    uint8_t temp;
    
    // Keep NMI if disabled
    ReadDirectIo(DeviceIoPortBased, CMOS_IO_BASE + CMOS_IO_SELECT, 1, &cmosValue);
    cmosValue &= CMOS_NMI_BIT;
    temp       = cmosValue & 0xFF;

    // Select cmosRegister (but do not change NMI)
    WriteDirectIo(DeviceIoPortBased, CMOS_IO_BASE + CMOS_IO_SELECT, 1, 
        (temp | (cmosRegister & CMOS_ALLBITS_NONMI)));
    ReadDirectIo(DeviceIoPortBased, CMOS_IO_BASE + CMOS_IO_DATA, 1, &cmosValue);
    temp = cmosValue & 0xFF;
    return temp;
}

void
CmosWrite(
    _In_ uint8_t cmosRegister,
    _In_ uint8_t data)
{
    size_t  cmosValue = 0;
    uint8_t temp;

    // Keep NMI if disabled
    ReadDirectIo(DeviceIoPortBased, CMOS_IO_BASE + CMOS_IO_SELECT, 1, &cmosValue);
    cmosValue &= CMOS_NMI_BIT;
    temp       = cmosValue & 0xFF;

    // Select cmosRegister (but do not change NMI)
    WriteDirectIo(DeviceIoPortBased, CMOS_IO_BASE + CMOS_IO_SELECT, 1,
        (temp | (cmosRegister & CMOS_ALLBITS_NONMI)));
    WriteDirectIo(DeviceIoPortBased, CMOS_IO_BASE + CMOS_IO_DATA, 1, data);
}

static void
__WaitForUpdate(
        _In_ void* context)
{
    _CRT_UNUSED(context);
    TRACE("__WaitForUpdate()");
    while (!(CmosRead(CMOS_REGISTER_STATUS_A) & CMOSA_TIME_UPDATING));
    while (CmosRead(CMOS_REGISTER_STATUS_A) & CMOSA_TIME_UPDATING);
}

static void
__RequestUpdate(
        _In_ void* context)
{
    uint8_t    statusB;
    irqstate_t irqstate;
    _CRT_UNUSED(context);
    TRACE("__RequestUpdate()");

    // Enable the update IRQ, this is automatically handled in RTC irq callback
    // as a one-shot irq, and cleared again.
    irqstate = InterruptDisable();
    statusB = CmosRead(CMOS_REGISTER_STATUS_B);
    statusB |= CMOSB_IRQ_UPDATE;
    (void)CmosRead(CMOS_REGISTER_STATUS_C);
    CmosWrite(CMOS_REGISTER_STATUS_B, statusB);
    (void)CmosRead(CMOS_REGISTER_STATUS_B);
    InterruptRestoreState(irqstate);
}

static void
__ReadSystemTime(
        _In_ void*         context,
        _In_ SystemTime_t* systemTime)
{
    uint8_t century = 0;
    uint8_t statusB = CmosRead(CMOS_REGISTER_STATUS_B);
    _CRT_UNUSED(context);

    if (g_cmos.AcpiCentury != 0) {
        century = CmosRead(g_cmos.AcpiCentury);
    }

    systemTime->Second     = CmosRead(CMOS_REGISTER_SECOND);
    systemTime->Minute     = CmosRead(CMOS_REGISTER_MINUTE);
    systemTime->Hour       = CmosRead(CMOS_REGISTER_HOUR);
    systemTime->DayOfMonth = CmosRead(CMOS_REGISTER_DAY_OF_MONTH);
    systemTime->Month      = CmosRead(CMOS_REGISTER_MONTH);
    systemTime->Year       = CmosRead(CMOS_REGISTER_YEAR);

    // Convert time format?
    if (!(statusB & CMOSB_FORMAT_BINARY)) {
        systemTime->Second     = CMOS_BCD_TO_DEC(systemTime->Second);
        systemTime->Minute     = CMOS_BCD_TO_DEC(systemTime->Minute);
        systemTime->Hour       = CMOS_BCD_TO_DEC(systemTime->Hour);
        systemTime->DayOfMonth = CMOS_BCD_TO_DEC(systemTime->DayOfMonth);
        systemTime->Month      = CMOS_BCD_TO_DEC(systemTime->Month);
        systemTime->Year       = CMOS_BCD_TO_DEC(systemTime->Year);
        if (century != 0) {
            century = CMOS_BCD_TO_DEC(century);
        }
    }

    // Correct the 0 indexed values
    systemTime->DayOfMonth++;

    if (century != 0) {
        systemTime->Year += century * 100;
    } else {
        systemTime->Year += (CMOS_CURRENT_YEAR / 100) * 100;
        if (systemTime->Year < CMOS_CURRENT_YEAR) {
            systemTime->Year += 100;
        }
    }
}
