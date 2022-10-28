/**
 * MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * Synchronization (Futex)
 */

#ifndef __FUTEX_H__
#define __FUTEX_H__

#include <os/types/syscall.h>
#include <os/futex.h>

KERNELAPI void KERNELABI
FutexInitialize(void);

/* FutexWait
 * Performs an atomic check-and-wait operation on the given atomic variable. It must match
 * the expected value otherwise the wait is ignored. */
KERNELAPI oserr_t KERNELABI
FutexWait(
        _In_ OSSyscallContext_t* syscallContext,
        _In_ _Atomic(int)*       futex,
        _In_ int                 expectedValue,
        _In_ int                 flags,
        _In_ _Atomic(int)*       futex2,
        _In_ int                 count,
        _In_ int                 operation,
        _In_ size_t              timeout);

/* FutexWake
 * Wakes up a blocked thread on the given atomic variable. */
KERNELAPI oserr_t KERNELABI
FutexWake(
    _In_ _Atomic(int)* Futex,
    _In_ int           Count,
    _In_ int           Flags);

/* FutexWakeOperation
 * Wakes up a blocked thread on the given atomic variable. */
KERNELAPI oserr_t KERNELABI
FutexWakeOperation(
    _In_ _Atomic(int)* Futex,
    _In_ int           Count,
    _In_ _Atomic(int)* Futex2,
    _In_ int           Count2,
    _In_ int           Operation,
    _In_ int           Flags);

#endif //!__FUTEX_H__
