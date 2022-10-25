/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * MollenOS System Component Infrastructure 
 * - The Interrupt Controller component. This component has the task
 *   of mapping and managing interrupts in the domain
 */

#ifndef __COMPONENT_INTERRUPT_CONTROLLER__
#define __COMPONENT_INTERRUPT_CONTROLLER__

#include <os/osdefs.h>

typedef struct SystemInterruptOverride {
    unsigned int OverrideFlags;
    int          SourceLine;
    int          DestinationLine;
} SystemInterruptOverride_t;

typedef struct SystemInterruptController {
    uuid_t      Id;
    int         InterruptLineBase;
    int         NumberOfInterruptLines;
    uintptr_t   MemoryAddress;
    uintptr_t   Data[4];

    struct SystemInterruptController* Link;
} SystemInterruptController_t;

/* CreateInterruptController
 * Creates a new interrupt controller with the given configuration and registers
 * with the system. */
KERNELAPI oserr_t KERNELABI
CreateInterruptController(
        _In_ uuid_t     Id,
        _In_ int        InterruptLineBase,
        _In_ int        NumberOfInterrupts,
        _In_ uintptr_t  BaseAddress);

/* CreateInterruptOverrides
 * Initializes the overrides with the given number of entries. */
KERNELAPI oserr_t KERNELABI
CreateInterruptOverrides(
    _In_ int        NumberOfInterruptOverrides);

/* RegisterInterruptOverride
 * Registers a new override in a free entry. If the entries are filled it returns OS_EUNKNOWN. */
KERNELAPI oserr_t KERNELABI
RegisterInterruptOverride(
    _In_ int        SourceInterruptLine,
    _In_ int        DestinationInterruptLine,
    _In_ unsigned int    InterruptFlags);

/* GetPinOffsetByLine
 * Retrieves the correct physical pin used by the given interrupt line. */
KERNELAPI int KERNELABI
GetPinOffsetByLine(
    _In_ int        InterruptLine);

/* GetInterruptControllerByLine
 * Retrieves the correct interrupt controller by identifying which line belongs to it. */
KERNELAPI SystemInterruptController_t* KERNELABI
GetInterruptControllerByLine(
    _In_ int        InterruptLine);

#endif // !__COMPONENT_INTERRUPT_CONTROLLER__
