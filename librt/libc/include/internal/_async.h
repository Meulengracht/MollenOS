/**
 * Copyright, Philip Meulengracht
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

#ifndef __INTERNAL_ASYNC_H__
#define __INTERNAL_ASYNC_H__

#include <os/types/async.h>

/**
 * @brief Wait for a forked syscall and return its completion status.
 *
 * @param status Result returned by the syscall.
 * @param async Syscall context. Must be valid when status is OS_EFORKED.
 * @return The completed syscall status, or status if the syscall did not fork.
 */
static inline oserr_t
__SyscallAwaitFork(
    _In_ oserr_t           status,
    _In_ OSAsyncContext_t* async)
{
    if (status != OS_EFORKED) {
        return status;
    }

    // The kernel child completes through the cooperative scheduler.
    usched_wait_async();
    return async->ErrorCode;
}

#endif
