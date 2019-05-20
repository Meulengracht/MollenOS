/* MollenOS
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Synchronization (Futex)
 */

#ifndef __FUTEX_H__
#define __FUTEX_H__

#include <os/osdefs.h>
#include <os/futex.h>

OsStatus_t
FutexWait(
    _In_ _Atomic(int)* Futex,
    _In_ int           ExpectedValue,
    _In_ int           Flags,
    _In_ size_t        Timeout);
    
OsStatus_t
FutexWaitOperation(
    _In_ _Atomic(int)* Futex,
    _In_ int           ExpectedValue,
    _In_ _Atomic(int)* Futex2,
    _In_ int           Count2,
    _In_ int           Operation,
    _In_ int           Flags,
    _In_ size_t        Timeout);

OsStatus_t
FutexWake(
    _In_ _Atomic(int)* Futex,
    _In_ int           Count,
    _In_ int           Flags);

OsStatus_t
FutexWakeOperation(
    _In_ _Atomic(int)* Futex,
    _In_ int           Count,
    _In_ _Atomic(int)* Futex2,
    _In_ int           Count2,
    _In_ int           Operation,
    _In_ int           Flags);

#endif //!__FUTEX_H__
