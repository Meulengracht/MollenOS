/**
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
 * High Performance Event Timer (HPET) Driver
 *  - Contains the implementation of the HPET driver for mollenos
 */
#ifndef _HPET_H_
#define _HPET_H_

#include <acpiinterface.h>
#include <os/osdefs.h>
#include <time.h>

/**
 * HPET Definitions
 * Magic constants and things like that which won't change
 */
#define HPET_MAXTIMERCOUNT		32
#define HPET_IOSPACE_LENGTH		0x800

#define HPET_MAXPERIOD			0x05F5E100
#define HPET_MAXTICK			100000000UL
#define HPET_MINTICK			100000UL

/**
 * HPET Registers
 * - General Registers
 */
#define HPET_REGISTER_CAPABILITIES		0x0000 // RO - Capabilities and ID Register
#define HPET_REGISTER_CONFIG			0x0010 // RW - General Configuration Register
#define HPET_REGISTER_INTSTATUS			0x0020 // WC - General Interrupt Status Register
#define HPET_REGISTER_MAINCOUNTER		0x00F0 // RW - Main Counter Value Register

/**
 * General Capabilities and ID Register
 * Bits  0 -  7: Revision
 * Bits  8 - 12: Number of timers in the HPET controller
 * Bit       13: If the main counter is 64 bit
 * Bit       15: If legacy mode is supported (RTC/PIT is emulated by this)
 * Bits 16 - 31: Vendor Id of HPET 
 * Bits 32 - 63: Main counter period tick
 */
#define HPET_REVISION(Capabilities)		((Capabilities) & 0xFF)
#define HPET_TIMERCOUNT(Capabilities)	(((Capabilities) >> 8) & 0x1F)
#define HPET_64BITSUPPORT				0x2000
#define HPET_LEGACYMODESUPPORT			0x8000
#define HPET_VENDORID(Capabilities)		(((Capabilities) >> 16) & 0xFFFF)
#define HPET_MAINPERIOD(Capabilities)	(((Capabilities) >> 32) & 0xFFFFFFFF)

/**
 * General Configuration Register
 * Bit        0: HPET Enabled/Disabled 
 * Bit        1: Legacy Mode Enabled/Disabled
 */
#define HPET_CONFIG_ENABLED		0x1
#define HPET_CONFIG_LEGACY		0x2

/**
 * Compartor Configuration
 * Bit        0: Reserved
 * Bit        1: Interrupt Polarity (1 - Level, 0 - Edge) 
 * Bit        2: Interrupt Enable/Disable 
 * Bit        3: Periodic Enable/Disable
 * Bit        4: Periodic Support
 * Bit        5: 64 Bit Support
 * Bit        6: Set Comparator Value Switch 
 * Bit        7: Reserved
 * Bit        8: 32 Bit Mode
 * Bit     9-13: Irq
 * Bit       14: MSI Enable/Disable
 * Bit       15: MSI Support
 * Bits 32 - 63: Interrupt Map
 */
#define HPET_TIMER_CONFIG_POLARITY			(1 << 1)
#define HPET_TIMER_CONFIG_IRQENABLED		(1 << 2)
#define HPET_TIMER_CONFIG_PERIODIC			(1 << 3)
#define HPET_TIMER_CONFIG_PERIODICSUPPORT	(1 << 4)
#define HPET_TIMER_CONFIG_64BITMODESUPPORT	(1 << 5)
#define HPET_TIMER_CONFIG_SET_CMP_VALUE		(1 << 6)
#define HPET_TIMER_CONFIG_32BITMODE			(1 << 8)
#define HPET_TIMER_CONFIG_IRQ(Irq)			(((Irq) & 0x1F) << 9)
#define HPET_TIMER_CONFIG_FSBMODE			(1 << 14)
#define HPET_TIMER_CONFIG_FSBSUPPORT		(1 << 15)
#define HPET_TIMER_CONFIG_IRQMAP			0xFFFFFFFF00000000

/**
 * Hpet Timer Access Macros
 * Use these to access a specific timer registers
 */
#define HPET_TIMER_CONFIG(Index)			((0x100 + (0x20 * (Index))))
#define HPET_TIMER_COMPARATOR(Index)		((0x108 + (0x20 * (Index))))
#define HPET_TIMER_FSB(Index)				((0x110 + (0x20 * (Index))))

/**
 * @brief Initializes the HPET if present. This requires the presence of ACPI tables.
 */
KERNELAPI void KERNELABI HPETInitialize(void);

/**
 * @brief Detects whether or not the HPET is present.
 * @return True if present, otherwise false.
 */
KERNELAPI bool KERNELABI HPETIsPresent(void);

/**
 * @brief Returns whether or not the hpet is configured to use legacy routings.
 * @return True if the HPET has legacy routings
 */
KERNELAPI bool KERNELABI HPETIsEmulatingLegacyController(void);

/**
 * @brief
 *
 * @param[In] index
 * @param[In] frequency
 * @param[In] periodic
 * @param[In] legacyIrq
 * @return
 */
KERNELAPI oserr_t KERNELABI
HPETComparatorStart(
    _In_ int      index,
    _In_ uint64_t frequency,
    _In_ int      periodic,
    _In_ int      legacyIrq);

/**
 * @brief Stops a previously started comparator.
 * @param index The comparator index to stop.
 * @return
 */
KERNELAPI oserr_t KERNELABI
HPETComparatorStop(
        _In_ int index);

#endif //!_HPET_H_
