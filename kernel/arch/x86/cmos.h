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
 */

#ifndef __DRIVER_CMOS_H__
#define __DRIVER_CMOS_H__

#include <os/osdefs.h>
#include <time.h>

#define CMOS_IO_BASE            0x70
#define CMOS_IO_LENGTH          2
#define CMOS_IO_SELECT          0x00
#define CMOS_IO_DATA            0x01

#define CMOS_REGISTER_SECONDS   0x00
#define CMOS_REGISTER_MINUTES   0x02
#define CMOS_REGISTER_HOURS     0x04
#define CMOS_REGISTER_DAYS      0x07
#define CMOS_REGISTER_MONTHS    0x08
#define CMOS_REGISTER_YEARS     0x09

#define CMOS_REGISTER_STATUS_A  0x0A
#define CMOS_REGISTER_STATUS_B  0x0B
#define CMOS_REGISTER_STATUS_C  0x0C
#define CMOS_REGISTER_STATUS_D  0x0D

#define CMOS_CURRENT_YEAR       2019

#define CMOS_NMI_BIT            0x80
#define CMOS_ALLBITS_NONMI      0x7F
#define CMOSX_BIT_DISABLE_NMI   0x80
#define CMOSA_UPDATE_IN_PROG    0x80
#define CMOSB_BCD_FORMAT        0x04
#define CMOSB_RTC_PERIODIC      0x40

#define CMOS_BCD_TO_DEC(n)      (((n >> 4) & 0x0F) * 10 + (n & 0x0F))
#define CMOS_DEC_TO_BCD(n)      (((n / 10) << 4) | (n % 10))

#define CMOS_RTC_IRQ            0x08

/* The CMOS driver structure
 * Contains information about the driver, the
 * current chip status, the current RTC status etc */
typedef struct Cmos {
    size_t  AlarmTicks;
    uint8_t AcpiCentury;
    int     RtcAvailable;

    // Rtc
    uint64_t NsCounter;
    clock_t  Ticks;
    size_t   NsTick;
    UUId_t   Irq;
} Cmos_t;

/* CmosInitialize
 * Initializes the cmos, and if set, the RTC as-well. */
KERNELAPI OsStatus_t KERNELABI
CmosInitialize(
    _In_ int InitializeRtc);

/* CmosRead
 * Read the byte at given register offset from the CMOS-Chip */
KERNELAPI uint8_t KERNELABI
CmosRead(
    _In_ uint8_t Register);

/* CmosWrite
 * Writes a byte to the given register offset from the CMOS-Chip */
KERNELAPI void KERNELABI
CmosWrite(
    _In_ uint8_t Register,
    _In_ uint8_t Data);

/* RtcInitialize
 * Initializes the rtc-part of the cmos chip and installs the interrupt needed */
KERNELAPI OsStatus_t KERNELABI
RtcInitialize(Cmos_t* Chip);

#endif // !__DRIVER_CMOS_H__
