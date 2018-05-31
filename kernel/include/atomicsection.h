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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS Synchronization
 *  - Atomic Locks are lightweight critical sections that do not
 *    support any reentrance but primary used as an irq-spinlock.
 */

#ifndef _MCORE_ATOMIC_SECTION_
#define _MCORE_ATOMIC_SECTION_

#include <os/osdefs.h>
#include <system/interrupts.h>

/* Atomic Section Definitions
 * Magic constants, bit definitions and types. */
#define ATOMICSECTION_INITIALIZE    { 0 }
typedef struct _AtomicSection {
    atomic_bool Status;
    IntStatus_t State;
} AtomicSection_t;

/* AtomicSectionEnter
 * Enters an atomic section that secures access untill Leave is called. */
KERNELAPI void KERNELABI
AtomicSectionEnter(
    _In_ AtomicSection_t* Section);

/* AtomicSectionLeave
 * Leaves an atomic section and allows other cpu's to grap the lock. */
KERNELAPI void KERNELABI
AtomicSectionLeave(
    _In_ AtomicSection_t* Section);

#endif
