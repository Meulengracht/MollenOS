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
#define INTERRUPT_MAXVECTORS			8

/* Interrupt allocation flags, interrupts are initially
 * always shareable */
#define INTERRUPT_SOFT				    0x00000001
#define INTERRUPT_VECTOR				0x00000002
#define INTERRUPT_MSI                   0x00000004
#define INTERRUPT_NOTSHARABLE           0x00000008
#define INTERRUPT_USERSPACE             0x00000010 // Slowest

/* The interrupt descriptor structure, this contains
 * information about the interrupt that needs to be registered
 * and special handling. */
typedef struct _MCoreInterrupt {
	// General information, note that these can change
	// after the RegisterInterruptSource, always use the value
	// in <Line> to see your allocated interrupt-line
	Flags_t					 AcpiConform;
	int						 Line;
	int						 Pin;

	// If the system should choose the best available
	// between all directs, fill all unused entries with 
	// INTERRUPT_NONE. Specify INTERRUPT_VECTOR to use this.
	int						 Vectors[INTERRUPT_MAXVECTORS];

    // Interrupt-handler(s) and context
    // FastHandler is called to determine whether or not this source
    // has produced the interrupt. 
    InterruptHandler_t		 FastHandler;
	void					*Data;

	// Read-Only
	uintptr_t				 MsiAddress;	// INTERRUPT_MSI - The address of MSI
	uintptr_t				 MsiValue;		// INTERRUPT_MSI - The value of MSI
} MCoreInterrupt_t;

/* RegisterInterruptSource 
 * Allocates the given interrupt source for use by
 * the requesting driver, an id for the interrupt source
 * is returned. After a succesful register, OnInterrupt
 * can be called by the event-system */
CRTDECL(
UUId_t,
RegisterInterruptSource(
	_In_ MCoreInterrupt_t *Interrupt, 
	_In_ Flags_t Flags));

/* UnregisterInterruptSource 
 * Unallocates the given interrupt source and disables
 * all events of OnInterrupt */
CRTDECL(
OsStatus_t,
UnregisterInterruptSource(
	_In_ UUId_t Source));

#endif //!_INTERRUPT_INTERFACE_H_
