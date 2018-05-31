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

#include <atomicsection.h>

/* AtomicSectionEnter
 * Enters an atomic section that secures access untill Leave is called. */
void
AtomicSectionEnter(
    _In_ AtomicSection_t* Section)
{
    bool locked = true;
    Section->State = InterruptDisable();
    while (1) {
        bool val = atomic_exchange(&Section->Status, locked);
        if (val == false) {
            break;
        }
    }
}

/* AtomicSectionLeave
 * Leaves an atomic section and allows other cpu's to grap the lock. */
void
AtomicSectionLeave(
    _In_ AtomicSection_t* Section)
{
    atomic_exchange(&Section->Status, false);
    InterruptRestoreState(Section->State);
}
