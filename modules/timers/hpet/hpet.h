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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS - High Performance Event Timer (HPET) Driver
 *  - Contains the implementation of the HPET driver for mollenos
 */
#ifndef _HPET_H_
#define _HPET_H_

/* Includes 
 * - Library */
#include <os/spinlock.h>
#include <os/osdefs.h>

/* Includes
 * - System */
#include <os/driver/contracts/base.h>
#include <os/driver/io.h>
#include <os/driver/interrupt.h>
#include <os/driver/device.h>
#include <os/driver/buffer.h>

/* HPET Definitions
 * Magic constants and things like that which won't change */
#define HPET_MAXTIMERCOUNT		32
#define HPET_MAXTICK			0x05F5E100
#define HPET_MAXPERIOD			100000000UL
#define HPET_MINPERIOD			100000UL

/* HPET Registers
 * - General Registers */
#define HPET_REGISTER_CAPABILITIES		0x0000 // RO - Capabilities and ID Register
#define HPET_REGISTER_CONFIG			0x0010 // RW - General Configuration Register
#define HPET_REGISTER_INTSTATUS			0x0020 // WC - General Interrupt Status Register
#define HPET_REGISTER_MAINCOUNTER		0x00F0 // RW - Main Counter Value Register

/* General Capabilities and ID Register
 * Bits  0 -  7: Revision
 * Bits  8 - 12: Number of timers in the HPET controller
 * Bit       13: If the main counter is 64 bit
 * Bit       15: If legacy mode is supported (RTC/PIT is emulated by this)
 * Bits 16 - 31: Vendor Id of HPET 
 * Bits 32 - 63: Main counter period tick */
#define HPET_REVISION(Capabilities)		(Capabilities & 0xFF)
#define HPET_TIMERCOUNT(Capabailities)	((Capabilities >> 8) & 0x1F)
#define HPET_64BITSUPPORT				0x2000
#define HPET_LEGACYMODESUPPORT			0x8000
#define HPET_VENDORID(Capabailities)	((Capabilities >> 16) & 0xFFFF)
#define HPET_MAINPERIOD(Capabailities)	((Capabilities >> 32) & 0xFFFFFFFF)

/* General Configuration Register
 * Bit        0: HPET Enabled/Disabled 
 * Bit        1: Legacy Mode Enabled/Disabled */
#define HPET_CONFIG_ENABLED		0x1
#define HPET_CONFIG_LEGACY		0x2

/* Compartor Configuration 
 * Bit        0: Reserved
 * Bit        1: Interrupt Polarity (1 - Level, 0 - Edge) 
 * Bit        2: Interrupt Enable/Disable 
 * Bit        3: Periodic Enable/Disable
 * Bit        4: Periodic Support
 * Bit        5: 64 Bit Support
 * Bit        6: Set Comparator Value Switch 
 * Bit        7: Reserved
 * Bit        8: 32 Bit Mode
 * Bit       14: MSI Enable/Disable
 * Bit       15: MSI Support
 * Bits 32 - 63: Interrupt Map */
#define HPET_TIMER_CONFIG_POLARITY			0x2
#define HPET_TIMER_CONFIG_IRQENABLED		0x4
#define HPET_TIMER_CONFIG_PERIODIC			0x8
#define HPET_TIMER_CONFIG_PERIODICSUPPORT	0x10
#define HPET_TIMER_CONFIG_64BITMODESUPPORT	0x20
#define HPET_TIMER_CONFIG_SET_CMP_VALUE		0x40
#define HPET_TIMER_CONFIG_32BITMODE			0x100
#define HPET_TIMER_CONFIG_FSBMODE			0x4000
#define HPET_TIMER_CONFIG_FSBSUPPORT		0x8000
#define HPET_TIMER_CONFIG_IRQMAP			0xFFFFFFFF00000000

/* Hpet Timer Access Macros
 * Use these to access a specific timer registers */
#define HPET_TIMER_CONFIG(Index)			((0x100 + (0x20 * Timer)))
#define HPET_TIMER_COMPARATOR(Index)		((0x108 + (0x20 * Timer)))
#define HPET_TIMER_FSBINTERRUPT(Index)		((0x110 + (0x20 * Timer)))

typedef struct _HpTimer {
	MCoreDevice_t			 Device;
	MContract_t				 Contract;
	UUId_t					 Interrupt;
	Spinlock_t				 Lock;

	int						 Present;
	int						 Enabled;
	int						 SystemTimer;
	reg32_t					 InterruptMap;

	int						 Is64Bit;
	int						 MsiSupport;
	int						 PeriodicSupport;
} HpTimer_t;

typedef struct _HpController {
	MCoreDevice_t			 Device;
	DeviceIoSpace_t			 IoSpace;
	HpTimer_t				 Timers[HPET_MAXTIMERCOUNT];

	size_t					 TickMinimum;
	LargeInteger_t			 Frequency;
	LargeInteger_t			 Clock;
} HpController_t;

/* HpControllerCreate 
 * Creates a new controller from the given device descriptor */
__EXTERN
HpController_t*
HpControllerCreate(
	_In_ MCoreDevice_t *Device);

/* HpControllerDestroy
 * Destroys an already registered controller and all its 
 * registers sub-timers */
__EXTERN
OsStatus_t
HpControllerDestroy(
	_In_ HpController_t *Controller);

/* HpRead
 * Reads the 32-bit value from the given register offset */
__EXTERN
OsStatus_t
HpRead(
	_In_ HpController_t *Controller, 
	_In_ size_t Offset,
	_Out_ reg32_t *Value);

/* HpWrite
 * Writes the given 32-bit value to the given register offset */
__EXTERN
OsStatus_t
HpWrite(
	_In_ HpController_t *Controller, 
	_In_ size_t Offset,
	_In_ reg32_t Value);

#endif //!_HPET_H_
