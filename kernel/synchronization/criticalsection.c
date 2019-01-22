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
 * MollenOS Synchronization
 *  - Critical Sections implementation used interrupt-disabling
 *    and spinlocks for synchronized access
 */
#define __MODULE "LOCK"

/* Includes 
 * - System */
#include <system/interrupts.h>
#include <system/utils.h>
#include <criticalsection.h>
#include <interrupts.h>
#include <debug.h>
#include <heap.h>

/* Includes
 * - Library */
#include <assert.h>

/* CriticalSectionCreate
 * Instantiate a new critical section with allocation and resets it */
CriticalSection_t*
CriticalSectionCreate(
    _In_ int Flags)
{
    // Variables
    CriticalSection_t *CSection = NULL;

    // Initialize a new instance
    CSection = (CriticalSection_t*)kmalloc(sizeof(CriticalSection_t));
    CriticalSectionConstruct(CSection, Flags);
    return CSection;
}

/* CriticalSectionConstruct
 * Constructs an already allocated section by resetting it to default state. */
void
CriticalSectionConstruct(
    _In_ CriticalSection_t *Section,
    _In_ int Flags)
{
    // Initiate all members to default state
    Section->Flags      = Flags;
    Section->State      = 0;
    Section->References = 0;
    Section->Owner      = UUID_INVALID;
    SpinlockReset(&Section->Lock);
}

/* CriticalSectionDestroy
 * Cleans up the lock by freeing resources allocated. */
void
CriticalSectionDestroy(
    _In_ CriticalSection_t *Section)
{
    _CRT_UNUSED(Section);
}

/* CriticalSectionEnter
 * Enters the critical state, the call will block untill lock is granted. */
OsStatus_t
CriticalSectionEnter(
    _In_ CriticalSection_t *Section)
{
    // Variables
    IntStatus_t IrqState = 0;

    // If the section is reentrancy supported, check
    // Critical sections are not owned by threads but instead cpu's as only
    // one cpu can own the lock at the time
    if (Section->References > 0 && (Section->Flags & CRITICALSECTION_REENTRANCY)) {
        if (Section->Owner == ArchGetProcessorCoreId()) {
            Section->References++;
            return OsSuccess;
        }
    }
    else if (Section->References > 0 && Section->Owner == ArchGetProcessorCoreId()) {
        FATAL(FATAL_SCOPE_KERNEL, "Tried to relock a non-recursive lock 0x%x", Section);
    }

    // Disable interrupts before we try to acquire the lock
    IrqState = InterruptDisable();
    SpinlockAcquire(&Section->Lock);

    // Update lock
    Section->Owner      = ArchGetProcessorCoreId();
    Section->References = 1;
    Section->State      = IrqState;
    return OsSuccess;
}

/* CriticalSectionLeave
 * Leave a critical section. The lock is only released on reaching
 * 0 references. */
OsStatus_t
CriticalSectionLeave(
    _In_ CriticalSection_t *Section)
{
    // Sanitize references
    assert(Section->References > 0);
    
    Section->References--;
    if (Section->References == 0) {
        Section->Owner = UUID_INVALID;
        SpinlockRelease(&Section->Lock);
        InterruptRestoreState(Section->State);
    }
    return OsSuccess;
}
