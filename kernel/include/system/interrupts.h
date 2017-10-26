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
 * MollenOS Interrupts Interface
 * - Contains the shared kernel interrupts interface
 *   that all sub-layers / architectures must conform to
 */

#ifndef _MCORE_SYSTEMINTS_H_
#define _MCORE_SYSTEMINTS_H_

/* Includes
 * - Library */
#include <os/osdefs.h>

/* Includes
 * - System */
#include <interrupts.h>

/* InterruptResolve 
 * Resolves the table index from the given interrupt settings. */
KERNELAPI
OsStatus_t
KERNELABI
InterruptResolve(
    _InOut_ MCoreInterrupt_t *Interrupt,
    _In_ Flags_t Flags,
    _Out_ UUId_t *TableIndex);

/* InterruptConfigure
 * Configures the given interrupt in the system */
KERNELAPI
OsStatus_t
KERNELABI
InterruptConfigure(
    _In_ MCoreInterruptDescriptor_t *Descriptor,
    _In_ int Enable);

/* InterruptDisable
 * Disables interrupts and returns
 * the state before disabling */
KERNELAPI
IntStatus_t
KERNELABI
InterruptDisable(void);

/* InterruptEnable
 * Enables interrupts and returns 
 * the state before enabling */
KERNELAPI
IntStatus_t
KERNELABI
InterruptEnable(void);

/* InterruptRestoreState
 * Restores the interrupt-status to the given
 * state, that must have been saved from SaveState */
KERNELAPI
IntStatus_t
KERNELABI
InterruptRestoreState(
    _In_ IntStatus_t State);

/* InterruptSaveState
 * Retrieves the current state of interrupts */
KERNELAPI
IntStatus_t
KERNELABI
InterruptSaveState(void);

/* InterruptIsDisabled
 * Returns 1 if interrupts are currently
 * disabled or 0 if interrupts are enabled */
KERNELAPI
int
KERNELABI
InterruptIsDisabled(void);

#endif //!_MCORE_SYSTEMINTS_H_
