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
* MollenOS x86-32 HPET Header
*/
#ifndef _X86_HPET_
#define _X86_HPET_


/* Includes */
#include <crtdefs.h>
#include <stdint.h>

/* Definitions */

/* General Capabilities and ID Register (ReadOnly) */
#define X86_HPET_REGISTER_CAP_ID	0x0000

/* General Configuration Register (ReadWrite) */
#define X86_HPET_REGISTER_CONFIG	0x0010

/* General Interrupt Status Register (ReadWriteClear) */
#define X86_HPET_REGISTER_INTR		0x0020

/* Main Counter Value Register (ReadWrite) */
#define X86_HPET_REGISTER_COUNTER	0x00F0


/* General Capabilities and ID Register (Bits) */
#define X86_HPET_CAP_REVISION		0xFF			/* Bits 0-7 */
#define X86_HPET_CAP_TIMERCOUNT		0x1F00			/* Bits 8-12 */
#define X86_HPET_CAP_64BITMODE		0x2000			/* Bit 13 */
#define X86_HPET_CAP_LEGACYMODE		0x8000			/* Bit 15 */
#define X86_HPET_CAP_VENDORID		0xFFFF0000		/* Bits 16-31 */
#define X86_HPET_CAP_PERIOD			0xFFFFFFFF00000000 /* This must not be 0, Bits 32-63 */

#define X86_HPET_MAXTICK			0x05F5E100

#define X86_HPET_MAX_PERIOD			100000000UL
#define X86_HPET_MIN_PERIOD         100000UL

/* General Configuration Register (Bits) */
#define X86_HPET_CONFIG_ENABLED		0x1
#define X86_HPET_CONFIG_LEGACY		0x2

/* Compartor Configuration (Bits) */
#define X86_HPET_TIMER_CONFIG_POLARITY			0x2
#define X86_HPET_TIMER_CONFIG_IRQENABLED		0x4
#define X86_HPET_TIMER_CONFIG_PERIODIC			0x8
#define X86_HPET_TIMER_CONFIG_PERIODICSUPPORT	0x10
#define X86_HPET_TIMER_CONFIG_64BITMODESUPPORT	0x20
#define X86_HPET_TIMER_CONFIG_SET_CMP_VALUE		0x40
#define X86_HPET_TIMER_CONFIG_32BITMODE			0x100
#define X86_HPET_TIMER_CONFIG_FSBMODE			0x4000
#define X86_HPET_TIMER_CONFIG_FSBSUPPORT		0x8000
#define X86_HPET_TIMER_CONFIG_IRQMAP			0xFFFFFFFF00000000

/* Hpet Timer Index */
#define X86_HPET_TIMER_REGISTER_CONFIG(Timer)		((0x100 + (0x20 * Timer)))
#define X86_HPET_TIMER_REGISTER_COMPARATOR(Timer)	((0x108 + (0x20 * Timer)))
#define X86_HPET_TIMER_REGISTER_FSBINTERRUPT(Timer)	((0x110 + (0x20 * Timer)))

/* Structures */
typedef struct _HpetObject
{
	/* Id */
	uint32_t Id;

	/* Device Id */
	DevId_t DeviceId;

	/* Irq it is mapped to */
	uint32_t Irq;

	/* Periodic Support */
	uint8_t Periodic;

	/* 64 Bit */
	uint8_t Bit64;

	/* Msi Support */
	uint16_t MsiSupport;

	/* Irq Map */
	uint32_t Map;

	/* Is in use? */
	uint32_t Active;

	/* Type? */
	uint32_t Type;

} Hpet_t;

/* Sleep & Stall */
_CRT_EXTERN uint64_t HpetGetClocks(void* Data);
_CRT_EXTERN void HpetSleep(void* Data, uint32_t MilliSeconds);
_CRT_EXTERN void HpetStall(void* Data, uint32_t MilliSeconds);

#endif