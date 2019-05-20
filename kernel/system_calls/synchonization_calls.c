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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
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

OsStatus_t
ScFutexWait(
    _In_ FutexParameters_t* Parameters)
{
    // Two version of wait
    if (Parameters->_flags & FUTEX_WAIT_OP) {
        return FutexWaitOperation(Parameters->_futex0, Parameters->_val0,
            Parameters->_futex1, Parameters->_val1, Parameters->_val2,
            Parameters->_flags, Parameters->_timeout);
    }
    return FutexWait(Parameters->_futex0, Parameters->_val0, Parameters->_flags,
        Parameters->_timeout);
}

OsStatus_t
ScFutexWake(
    _In_ FutexParameters_t* Parameters)
{
    // Also two versions of wake
    if (Parameters->_flags & FUTEX_WAKE_OP) {
        return FutexWakeOperation(Parameters->_futex0, Parameters->_val0,
            Parameters->_futex1, Parameters->_val1, Parameters->_val2,
            Parameters->_flags);
    }
    return FutexWake(Parameters->_futex0, Parameters->_val0, Parameters->_flags);
}
