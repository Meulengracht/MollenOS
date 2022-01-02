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

#define CMOS_REGISTER_SECOND        0x00
#define CMOS_REGISTER_SECOND_ALARM  0x01
#define CMOS_REGISTER_MINUTE        0x02
#define CMOS_REGISTER_MINUTE_ALARM  0x03
#define CMOS_REGISTER_HOUR          0x04
#define CMOS_REGISTER_HOUR_ALARM    0x05
#define CMOS_REGISTER_DAY_OF_WEEK   0x06
#define CMOS_REGISTER_DAY_OF_MONTH  0x07
#define CMOS_REGISTER_MONTH         0x08
#define CMOS_REGISTER_YEAR          0x09

#define CMOS_REGISTER_STATUS_A  0x0A
#define CMOS_REGISTER_STATUS_B  0x0B
#define CMOS_REGISTER_STATUS_C  0x0C
#define CMOS_REGISTER_STATUS_D  0x0D // Only bit 7 is used to detect whether CMOS has power

#define CMOS_CURRENT_YEAR       2022

/**
 * CMOS Status Register A
 * Bits 0-3: Rate Selection
 * Bits 4-6: Divider
 * Bit  7:   Time update in progress
 */
#define CMOSA_TIME_UPDATING    0x80


#define CMOS_NMI_BIT            0x80
#define CMOS_ALLBITS_NONMI      0x7F
#define CMOSX_BIT_DISABLE_NMI   0x80

/**
 * CMOS Status Register B
 * Bit 0: Enable Daylight Savings
 * Bit 1: (1) 24 Hour Mode (0) 12 Hour Mode
 * Bit 2: Time/Date Format (1) Binary (0) BCD
 * Bit 3: Square Wave Frequency (1) Enable (0) Disable
 * Bit 4: Update Ended Interrupt
 * Bit 5: Alarm Interrupt
 * Bit 6: Periodic Interrupt
 * Bit 7: Clock Update (1) Disable (0) Update Count Normally
 */
#define CMOSB_BCD_FORMAT        0x04
#define CMOSB_RTC_PERIODIC      0x40

/**
 * CMOS Status Register C (ReadOnly)
 * Bits 0-3: Reserved
 * Bit  4:   Update Ended Interrupt Enabled
 * Bit  5:   Alarm Interrupt Enabled
 * Bit  6:   Periodic Interrupt Enabled
 * Bit  7:   IRQF
 */


/**
 * CMOS Conversion (BCD) Macros
 */
#define CMOS_BCD_TO_DEC(n) ((((n) >> 4) & 0x0F) * 10 + ((n) & 0x0F))
#define CMOS_DEC_TO_BCD(n) ((((n) / 10) << 4) | ((n) % 10))

/**
 * CMOS Interrupt Line (Fixed-ISA)
 */
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
