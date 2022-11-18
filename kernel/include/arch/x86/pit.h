/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS X86 PIT (Timer) Driver
 * http://wiki.osdev.org/PIT
 */

#ifndef __DRIVER_PIT_H__
#define __DRIVER_PIT_H__

#include <os/osdefs.h>
#include <time.h>

/* Io-space for accessing the PIT
 * Spans over 4 bytes from 0x40-0x44 */
#define PIT_IO_BASE					0x40
#define PIT_IO_LENGTH				0x04
#define	PIT_REGISTER_COUNTER0		0x00
#define PIT_REGISTER_COMMAND		0x03

/* Bit definitions for the command register 
 * Bits	  0: BCD/Binary mode: 0 = 16-bit binary, 1 = four-digit BCD 
 * Bits 1-3: Operating Mode 
 * Bits 4-5: Access mode 
 * Bits 6-7: Channel */
#define PIT_COMMAND_BCD				0x1

#define PIT_COMMAND_MODE0			0x0		/* Interrupt on terminal count */
#define PIT_COMMNAD_MODE1			0x2		/* hardware re-triggerable one-shot */
#define PIT_COMMAND_MODE2			0x4		/* rate generator */
#define PIT_COMMAND_MODE3			0x6		/* square wave generator */
#define PIT_COMMNAD_MODE4			0x8		/* software triggered strobe */
#define PIT_COMMAND_MODE5			0xA		/* hardware triggered strobe */
#define PIT_COMMAND_MODE6			0xC		/* rate generator, same as 010b */
#define PIT_COMMAND_MODE7			0xF		/* square wave generator, same as 011b */

#define PIT_COMMAND_LATCHCOUNT		0x0		/* Latch count value command */
#define PIT_COMMAND_LOWBYTE			0x10	/* Access mode: lobyte only */
#define PIT_COMMAND_HIGHBYTE		0x20	/* Access mode: hibyte only */
#define	PIT_COMMAND_FULL			0x30	/* Access mode: lobyte/hibyte */

#define	PIT_COMMAND_COUNTER_0		0
#define	PIT_COMMAND_COUNTER_1		0x40
#define	PIT_COMMAND_COUNTER_2		0x80

#define PIT_FREQUENCY               1193181
#define PIT_FREQUENCY_LOWEST        18       // The lowest supported frequency by PIT is 18 hz (0 divisor)
#define PIT_FREQUENCY_HIGHEST       10000

/* The IRQ line the PIT uses, it's an ISA line, so it's fixed */
#define PIT_IRQ						0x0

/**
 * @brief Initializes the Programmable Interval Timer. The behaviour of
 * the timers and how they are configured depends on whether the RTC chip
 * is available as well for CMOS time updates.
 *
 * @param[In] rtcAvailable Availability of the RTC chip
 * @return    Status of the initialization.
 */
KERNELAPI oserr_t KERNELABI
PitInitialize(
        _In_ int rtcAvailable);

/**
 * @brief Enables or disables calibration mode for the PIT. If calibration mode is enabled
 * the calibration ticker will tick each 1ms.
 *
 * @param[In] enable Set to non-zero to enable calibration mode.
 */
KERNELAPI void KERNELABI
PitSetCalibrationMode(
        _In_ int enable);

#endif //!__DRIVER_PIT_H___
