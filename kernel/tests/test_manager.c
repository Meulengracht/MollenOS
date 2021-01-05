/* MollenOS
 *
 * Copyright 2011 - 2018, Philip Meulengracht
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
 * OS Testing Suite
 *  - Synchronization tests to verify the robustness and correctness of concurrency.
 */
#define __MODULE "TEST"
#define __TRACE

#include <threading.h>
#include <debug.h>

void
StartTestingPhase(void)
{
    //UUId_t CurrentTest;
    TRACE("StartTestingPhase()");

    // Run data-structure tests
    //TRACE(" > Running data structure tests");
    //CurrentTest = ThreadCreate("TestDataStructures", TestDataStructures, NULL, 0);
    //ThreadJoin(CurrentTest);

    // Run synchronization tests
    //TRACE(" > Running synchronization tests");
    //if (ThreadCreate("TestSynchronization", TestSynchronization, NULL, 0, UUID_INVALID, &CurrentTest) != OsSuccess) {
    //    ERROR(" > Failed to spawn test thread");
    //    return;
    //}
    //ThreadJoin(CurrentTest);
}
