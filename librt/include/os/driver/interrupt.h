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
 * MollenOS MCore - Interrupt Support Definitions & Structures
 * - This header describes the base interrupt-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _INTERRUPT_INTERFACE_H_
#define _INTERRUPT_INTERFACE_H_

/* Includes
 * - C-Library */
#include <os/osdefs.h>

/* Includes
 * - System */
#include <os/driver/device.h>

/* Specail interrupt constants, use these when allocating
 * interrupts if neccessary */
#define INTERRUPT_NONE					(int)-1;

/* RegisterInterruptSource 
 * Allocates the given interrupt source for use by
 * the requesting driver, an id for the interrupt source
 * is returned. After a succesful register, OnInterrupt
 * can be called by the event-system */
_MOS_API UUId_t RegisterInterruptSource(int Line,
	int Pin, int Direct[__DEVICEMANAGER_MAX_IRQS]);

/* UnregisterInterruptSource 
 * Unallocates the given interrupt source and disables
 * all events of OnInterrupt */
_MOS_API OsStatus_t UnregisterInterruptSource(UUId_t Source);

#endif //!_INTERRUPT_INTERFACE_H_
