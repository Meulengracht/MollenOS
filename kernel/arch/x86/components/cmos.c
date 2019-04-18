/* MollenOS
 *
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * X86 CMOS & RTC (Clock) Driver
 * http://wiki.osdev.org/CMOS#The_Real-Time_Clock
 */
#define __MODULE "CMOS"
#define __TRACE

#include <os/mollenos.h>
#include <acpiinterface.h>
#include "../cmos.h"
#include <arch/io.h>
#include <string.h>
#include <debug.h>

static Cmos_t CmosUnit = { 0 };

OsStatus_t
CmosInitialize(
    _In_ int InitializeRtc)
{
    TRACE("CmosInitialize(rtc %" PRIiIN ")", InitializeRtc);

    // Check out century register
    if (AcpiAvailable() == ACPI_AVAILABLE) {
        CmosUnit.AcpiCentury = AcpiGbl_FADT.Century;
    }
    CmosUnit.RtcAvailable = InitializeRtc;

    // Last part is to initialize the rtc chip if it present in system
    if (CmosUnit.RtcAvailable) {
        return RtcInitialize(&CmosUnit);
    }
    else {
        return OsSuccess;
    }
}

uint8_t
CmosRead(
    _In_ uint8_t Register)
{
    size_t  Storage = 0;
    uint8_t Tmp     = 0;
    
    // Keep NMI if disabled
    ReadDirectIo(DeviceIoPortBased, CMOS_IO_BASE + CMOS_IO_SELECT, 1, &Storage);
    Storage &= CMOS_NMI_BIT;
    Tmp     = Storage & 0xFF;

    // Select Register (but do not change NMI)
    WriteDirectIo(DeviceIoPortBased, CMOS_IO_BASE + CMOS_IO_SELECT, 1, 
        (Tmp | (Register & CMOS_ALLBITS_NONMI)));
    ReadDirectIo(DeviceIoPortBased, CMOS_IO_BASE + CMOS_IO_DATA, 1, &Storage);
    Tmp = Storage & 0xFF;
    return Tmp;
}

void
CmosWrite(
    _In_ uint8_t Register,
    _In_ uint8_t Data)
{
    size_t  Storage  = 0;
    uint8_t Tmp     = 0;

    // Keep NMI if disabled
    ReadDirectIo(DeviceIoPortBased, CMOS_IO_BASE + CMOS_IO_SELECT, 1, &Storage);
    Storage &= CMOS_NMI_BIT;
    Tmp     = Storage & 0xFF;

    // Select Register (but do not change NMI)
    WriteDirectIo(DeviceIoPortBased, CMOS_IO_BASE + CMOS_IO_SELECT, 1,
        (Tmp | (Register & CMOS_ALLBITS_NONMI)));
    WriteDirectIo(DeviceIoPortBased, CMOS_IO_BASE + CMOS_IO_DATA, 1, Data);
}

void
CmosResetTicks(void)
{
    CmosUnit.Ticks = 0;
}

clock_t
CmosGetTicks(void)
{
    return CmosUnit.Ticks;
}

OsStatus_t
ArchSynchronizeSystemTime(void)
{
    while (!(CmosRead(CMOS_REGISTER_STATUS_A) & CMOSA_UPDATE_IN_PROG));
    while (CmosRead(CMOS_REGISTER_STATUS_A) & CMOSA_UPDATE_IN_PROG);
    return OsSuccess;
}

OsStatus_t
ArchGetSystemTime(
    _In_ SystemTime_t* SystemTime)
{
    uint8_t Century = 0;
    uint8_t StatusB = CmosRead(CMOS_REGISTER_STATUS_B);

    if (CmosUnit.AcpiCentury != 0) {
        Century = CmosRead(CmosUnit.AcpiCentury);
    }

    // Wait while update is in progress
    while (CmosRead(CMOS_REGISTER_STATUS_A) & CMOSA_UPDATE_IN_PROG);

    // Fill in variables
    SystemTime->Second     = CmosRead(CMOS_REGISTER_SECONDS);
    SystemTime->Minute     = CmosRead(CMOS_REGISTER_MINUTES);
    SystemTime->Hour       = CmosRead(CMOS_REGISTER_HOURS);
    SystemTime->DayOfMonth = CmosRead(CMOS_REGISTER_DAYS);
    SystemTime->Month      = CmosRead(CMOS_REGISTER_MONTHS);
    SystemTime->Year       = CmosRead(CMOS_REGISTER_YEARS);

    // Convert time format? 
    if (!(StatusB & CMOSB_BCD_FORMAT)) {
        SystemTime->Second     = CMOS_BCD_TO_DEC(SystemTime->Second);
        SystemTime->Minute     = CMOS_BCD_TO_DEC(SystemTime->Minute);
        SystemTime->Hour       = CMOS_BCD_TO_DEC(SystemTime->Hour);
        SystemTime->DayOfMonth = CMOS_BCD_TO_DEC(SystemTime->DayOfMonth);
        SystemTime->Month      = CMOS_BCD_TO_DEC(SystemTime->Month);
        SystemTime->Year       = CMOS_BCD_TO_DEC(SystemTime->Year);
        if (Century != 0) {
            Century = CMOS_BCD_TO_DEC(Century);
        }    
    }

    // Correct the 0 indexed values
    SystemTime->DayOfMonth++;

    if (Century != 0) {
        SystemTime->Year += Century * 100;
    }
    else {
        SystemTime->Year += (CMOS_CURRENT_YEAR / 100) * 100;
        if (SystemTime->Year < CMOS_CURRENT_YEAR) {
            SystemTime->Year += 100;
        }
    }
    return OsSuccess;
}
