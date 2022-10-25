/**
 * MollenOS
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
 * System Component Infrastructure 
 * - The Interrupt Controller component. This component has the task
 *   of mapping and managing interrupts in the domain
 */

#include <assert.h>
#include <heap.h>
#include <interrupts.h>
#include <machine.h>
#include <string.h>

oserr_t
CreateInterruptController(
        _In_ uuid_t     Id,
        _In_ int        InterruptLineBase,
        _In_ int        NumberOfInterrupts,
        _In_ uintptr_t  BaseAddress)
{
    // Variables
    SystemInterruptController_t *Ic;

    Ic = (SystemInterruptController_t*)kmalloc(sizeof(SystemInterruptController_t));
    memset((void*)Ic, 0, sizeof(SystemInterruptController_t));

    Ic->Id                      = Id;
    Ic->InterruptLineBase       = InterruptLineBase;
    Ic->NumberOfInterruptLines  = NumberOfInterrupts;
    Ic->MemoryAddress           = BaseAddress;

    if (GetMachine()->InterruptController == NULL) {
        GetMachine()->InterruptController = Ic;
    }
    else {
        SystemInterruptController_t *IcHead = GetMachine()->InterruptController;
        while (IcHead->Link != NULL) {
            IcHead = IcHead->Link;
        }
        IcHead->Link = Ic;
    }
    return OS_EOK;
}

oserr_t
CreateInterruptOverrides(
    _In_ int        NumberOfInterruptOverrides)
{
    int i;

    assert(GetMachine()->Overrides == NULL);
    assert(NumberOfInterruptOverrides > 0);

    // Allocate the number of interrupts
    GetMachine()->NumberOfOverrides = NumberOfInterruptOverrides;
    GetMachine()->Overrides         = (SystemInterruptOverride_t*)kmalloc(
        sizeof(SystemInterruptOverride_t) * NumberOfInterruptOverrides);

    // Set them unused
    for (i = 0; i < NumberOfInterruptOverrides; i++) {
        GetMachine()->Overrides[i].SourceLine = -1;
    }
    return OS_EOK;
}

oserr_t
RegisterInterruptOverride(
    _In_ int        SourceInterruptLine,
    _In_ int        DestinationInterruptLine,
    _In_ unsigned int    InterruptFlags)
{
    int i;
    
    assert(GetMachine()->NumberOfOverrides > 0);

    // Find an unused entry and store it there
    for (i = 0; i < GetMachine()->NumberOfOverrides; i++) {
        if (GetMachine()->Overrides[i].SourceLine == -1) {
            GetMachine()->Overrides[i].SourceLine       = SourceInterruptLine;
            GetMachine()->Overrides[i].DestinationLine  = DestinationInterruptLine;
            GetMachine()->Overrides[i].OverrideFlags    = ConvertAcpiFlagsToConformFlags(InterruptFlags, DestinationInterruptLine);
            return OS_EOK;
        }
    }
    return OS_EUNKNOWN;
}

int
GetPinOffsetByLine(
    _In_ int        InterruptLine)
{
    SystemInterruptController_t *Ic = GetMachine()->InterruptController;
    while (Ic != NULL) {
        if (InterruptLine >= Ic->InterruptLineBase && 
            InterruptLine < (Ic->InterruptLineBase + Ic->NumberOfInterruptLines)) {
            return InterruptLine - Ic->InterruptLineBase;
        }
        Ic = Ic->Link;
    }
    return INTERRUPT_NONE;
}

SystemInterruptController_t*
GetInterruptControllerByLine(
    _In_ int        InterruptLine)
{
    SystemInterruptController_t *Ic = GetMachine()->InterruptController;
    while (Ic != NULL) {
        if (InterruptLine >= Ic->InterruptLineBase && 
            InterruptLine < (Ic->InterruptLineBase + Ic->NumberOfInterruptLines)) {
            return Ic;
        }
        Ic = Ic->Link;
    }
    return NULL;
}
