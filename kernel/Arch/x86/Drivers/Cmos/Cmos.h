/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS X86-32 CMOS & RTC Driver
*/

#ifndef _X86_CMOS_RTC_H_
#define _X86_CMOS_RTC_H_

/* Includes */
#include <crtdefs.h>
#include <time.h>
#include <stdint.h>

/* Definitions */
#define X86_CMOS_IO_SELECT			0x70
#define X86_CMOS_IO_DATA			0x71

#define X86_CMOS_REGISTER_SECONDS	0x00
#define X86_CMOS_REGISTER_MINUTES	0x02
#define X86_CMOS_REGISTER_HOURS		0x04
#define X86_CMOS_REGISTER_DAYS		0x07
#define X86_CMOS_REGISTER_MONTHS	0x08
#define X86_CMOS_REGISTER_YEARS		0x09

#define X86_CMOS_REGISTER_STATUS_A	0x0A
#define X86_CMOS_REGISTER_STATUS_B	0x0B
#define X86_CMOS_REGISTER_STATUS_C	0x0C
#define X86_CMOS_REGISTER_STATUS_D	0x0D

#define X86_CMOS_CURRENT_YEAR		2014

/* Bits */
#define X86_CMOSX_BIT_DISABLE_NMI	0x80
#define X86_CMOSA_UPDATE_IN_PROG	0x80
#define X86_CMOSB_BCD_FORMAT		0x04
#define X86_CMOSB_RTC_PERIODIC		0x40

/* Time Conversion */
#define X86_CMOS_BCD_TO_DEC(n)		(((n >> 4) & 0x0F) * 10 + (n & 0x0F))
#define X86_CMOS_DEC_TO_BCD(n)		(((n / 10) << 4) | (n % 10))

/* RTC Irq */
#define X86_CMOS_RTC_IRQ			0x08

/* Prototypes */
_CRT_EXTERN uint8_t clock_read_register(uint8_t reg);
_CRT_EXTERN void clock_write_register(uint8_t reg, uint8_t data);

_CRT_EXTERN void clock_get_time(tm *t);
_CRT_EXTERN uint64_t clock_get_clocks(void);
_CRT_EXTERN void clock_stall(uint32_t ms);
_CRT_EXTERN void clock_stall_noint(uint32_t ms);

_CRT_EXTERN void clock_init(void);

#endif // !_X86_CMOS_RTC_H_
