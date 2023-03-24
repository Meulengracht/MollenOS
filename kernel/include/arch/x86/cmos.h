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
 * Bits 0-3: Rate Selection (Square wave frequency)
 * Bits 4-6: Divider
 * Bit  7:   Time update in progress
 */
#define CMOSA_TIME_UPDATING    0x80

/**
 * Rate selection Bits for divider output
 * 32768 >> (Rate - 1)
 * 15 = 2, 14 = 4, 13 = 8/s (125 ms), 12 = 16, 11 = 32, 10 = 64, 9 = 128
 * 8 = 256, 7 = 512, 6 = 1024, 5 = 2048, 4 = 4096, 3 = 8192, ...
 *
 * 22 stage divider, time base being used
 * 2 (0x20) = 32.768kHz
 */
#define CMOSA_DIVIDER_32KHZ 0x20

#define RTC_FREQUENCY_LOWEST  2    // Lowest supported is 2hz
#define RTC_FREQUENCY_HIGHEST 8192 // Highest possible precision we can get on most machines

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
#define CMOSB_DL_SAVINGS    0x01
#define CMOSB_FORMAT_24HOUR 0x02
#define CMOSB_FORMAT_BINARY 0x04
#define CMOSB_IRQ_SQWAVFRQ  0x08
#define CMOSB_IRQ_UPDATE    0x10
#define CMOSB_IRQ_ALARM     0x20
#define CMOSB_IRQ_PERIODIC  0x40
#define CMOSB_CLOCK_DISABLE 0x80

/**
 * CMOS Status Register C (ReadOnly)
 * Bits 0-3: Reserved
 * Bit  4:   Update Ended Interrupt Enabled
 * Bit  5:   Alarm Interrupt Enabled
 * Bit  6:   Periodic Interrupt Enabled
 * Bit  7:   IRQF
 */
#define CMOSC_IRQ_UPDATE   0x10
#define CMOSC_IRQ_ALARM    0x20
#define CMOSC_IRQ_PERIODIC 0x40
#define CMOSC_IRQ_FLAG     0x80

/**
 * CMOS Conversion (BCD) Macros
 */
#define CMOS_BCD_TO_DEC(n) ((((n) >> 4) & 0x0F) * 10 + ((n) & 0x0F))
#define CMOS_DEC_TO_BCD(n) ((((n) / 10) << 4) | ((n) % 10))

/**
 * CMOS Interrupt Line (Fixed-ISA)
 */
#define CMOS_RTC_IRQ 0x08

typedef struct Cmos {
    uint8_t  AcpiCentury;
    bool     RtcAvailable;
    bool     RtcEnabled;
    int      InterruptLine;
    uuid_t   Irq;
    uint32_t Frequency;
    uint64_t Ticks;
} Cmos_t;

/**
 * @brief Initializes the cmos, and if set, the RTC as-well.
 *
 * @param[In] initializeRtc Whether to initialize the RTC chip
 * @return    Status of the initialization.
 */
KERNELAPI oserr_t KERNELABI
CmosInitialize(
    _In_ int initializeRtc);

/**
 * @brief Reads a byte value from a specific CMOS register.
 *
 * @param[In] cmosRegister
 * @return
 */
KERNELAPI uint8_t KERNELABI
CmosRead(
    _In_ uint8_t cmosRegister);

/**
 * @brief Writes a byte value to a specific CMOS register.
 *
 * @param[In] cmosRegister
 * @param[In] data
 */
KERNELAPI void KERNELABI
CmosWrite(
    _In_ uint8_t cmosRegister,
    _In_ uint8_t data);

/**
 * @brief Initializes the RTC chip and neccessary interrupts. The RTC will be initialized
 * to CalibrationMode and will remain in such untill
 *
 * @param[In] cmos The CMOS chip instance that gets initialized
 * @return    Status of the initialization.
 */
KERNELAPI oserr_t KERNELABI
RtcInitialize(
        _In_ Cmos_t* cmos);

/**
 * @brief Enables or disables calibration mode for the RTC. If calibration mode is enabled
 * the calibration ticker will tick each 1ms.
 *
 * @param[In] enable Set to non-zero to enable calibration mode.
 */
KERNELAPI void KERNELABI
RtcSetCalibrationMode(
        _In_ int enable);

#endif // !__DRIVER_CMOS_H__
