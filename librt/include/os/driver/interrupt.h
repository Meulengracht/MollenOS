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

/* Interrupt handler signature, this is only used for
 * fast-interrupts that does not need interrupts enabled
 * or does any processing work */
#ifndef __INTERRUPTHANDLER
#define __INTERRUPTHANDLER
typedef InterruptStatus_t(*InterruptHandler_t)(void*);
#endif

/* Specail interrupt constants, use these when allocating
 * interrupts if neccessary */
#define INTERRUPT_NONE					(int)-1
#define INTERRUPT_MAXDIRECTS			8

/* Interrupt allocation flags, interrupts are initially
 * always shareable */
#define INTERRUPT_NOTSHARABLE			0x1
#define INTERRUPT_FAST					0x2

/* The interrupt descriptor structure, this contains
 * information about the interrupt that needs to be registered
 * and special handling. */
typedef struct _MCoreInterrupt {
	Flags_t					AcpiConform;
	int						Line;
	int						Pin; 
	int						Direct[INTERRUPT_MAXDIRECTS];

	InterruptHandler_t		FastHandler;
	void					*Data;
} MCoreInterrupt_t;

/* InitializeInterrupt
 * Initializes the interrupt from a given device
 * and fills out the correct information from the
 * device-structure */
MOSAPI
OsStatus_t
MOSABI
InitializeInterrupt(
	_Out_ MCoreInterrupt_t *Interrupt,
	_In_ MCoreDevice_t *Device);

/* RegisterInterruptSource 
 * Allocates the given interrupt source for use by
 * the requesting driver, an id for the interrupt source
 * is returned. After a succesful register, OnInterrupt
 * can be called by the event-system */
MOSAPI
UUId_t
MOSABI
RegisterInterruptSource(
	_In_ MCoreInterrupt_t *Interrupt, 
	_In_ Flags_t Flags);

/* UnregisterInterruptSource 
 * Unallocates the given interrupt source and disables
 * all events of OnInterrupt */
MOSAPI
OsStatus_t
MOSABI
UnregisterInterruptSource(
	_In_ UUId_t Source);

#endif //!_INTERRUPT_INTERFACE_H_
