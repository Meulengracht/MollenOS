/**
 * Copyright 2022, Philip Meulengracht
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

#include <os/futex.h>
#include <internal/_syscalls.h>

oserr_t
OSFutex(
        _In_ OSFutexParameters_t* parameters,
        _In_ OSAsyncContext_t*    asyncContext)
{
    oserr_t oserr;

    if (FUTEX_FLAG_ACTION(parameters->_flags) == FUTEX_FLAG_WAIT) {
        oserr = Syscall_FutexWait(asyncContext, parameters);
    } else {
        return Syscall_FutexWake(parameters);
    }

    if (oserr == OS_EFORKED) {
        // The system call was postponed, so we should coordinate with the
        // userspace threading system right here.
        usched_wait_async(asyncContext);
        return asyncContext->ErrorCode;
    }
    return oserr;
}
