/* MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 *
 * System call implementations (Synchronization)
 *
 */
#define __MODULE "SCIF"
//#define __TRACE

#include <internal/_utils.h>
#include <os/osdefs.h>
#include <futex.h>
#include <userevent.h>

oscode_t
ScFutexWait(
    _In_ FutexParameters_t* parameters)
{
    // Two version of wait
    if (parameters->_flags & FUTEX_WAIT_OP) {
        return FutexWaitOperation(parameters->_futex0, parameters->_val0,
                                  parameters->_futex1, parameters->_val1, parameters->_val2,
                                  parameters->_flags, parameters->_timeout);
    }
    return FutexWait(parameters->_futex0, parameters->_val0, parameters->_flags,
                     parameters->_timeout);
}

oscode_t
ScFutexWake(
    _In_ FutexParameters_t* parameters)
{
    // Also two versions of wake
    if (parameters->_flags & FUTEX_WAKE_OP) {
        return FutexWakeOperation(parameters->_futex0, parameters->_val0,
                                  parameters->_futex1, parameters->_val1, parameters->_val2,
                                  parameters->_flags);
    }
    return FutexWake(parameters->_futex0, parameters->_val0, parameters->_flags);
}

oscode_t ScEventCreate(unsigned int initialValue, unsigned int flags, uuid_t* handleOut, atomic_int** syncAddressOut)
{
    return UserEventCreate(initialValue, flags, handleOut, syncAddressOut);
}
