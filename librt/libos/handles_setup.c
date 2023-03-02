/**
 * Copyright 2023, Philip Meulengracht
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
 */

#include <assert.h>
#include <os/handle.h>

extern void    OSHandlesInitialize(void);
extern oserr_t OSHandlesRegisterHandlers(uint32_t type, OSHandleDestroyFn, OSHandleSerializeFn, OSHandleDeserializeFn);

// Event callbacks
extern void OSEventDctor(OSHandle_t*);

void OSHandlesSetup(void)
{
    oserr_t oserr;

    // Initialize handle subsystem
    OSHandlesInitialize();

    // Register all callbacks

    // Events
    oserr = OSHandlesRegisterHandlers(OSHANDLE_EVENT, OSEventDctor, NULL, NULL);
    assert(oserr == OS_EOK);
}
