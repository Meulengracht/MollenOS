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
 * Spinlock Support Definitions & Structures
 * - This header describes the base spinlock-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __SPINLOCK_H__
#define __SPINLOCK_H__

#include <os/osdefs.h>

#define SPINLOCK_RECURSIVE 0x1

typedef struct {
    int          Value;
    Flags_t      Configuration;
    UUId_t       Owner;
    _Atomic(int) References;
} Spinlock_t;

#define SPINLOCK_INIT(Flags) { 0, Flags, UUID_INVALID, 0 }

_CODE_BEGIN
/* SpinlockReset
 * This initializes a spinlock handle and sets it to default value (unlocked) */
CRTDECL(OsStatus_t,
SpinlockReset(
	_In_ Spinlock_t* Lock,
    _In_ Flags_t     Configuration));

/* SpinlockAcquire
 * Acquires the spinlock while busy-waiting for it to be ready if neccessary */
CRTDECL(void,
SpinlockAcquire(
	_In_ Spinlock_t* Lock));

/* SpinlockTryAcquire
 * Makes an attempt to acquire the spinlock without blocking */
CRTDECL(OsStatus_t,
SpinlockTryAcquire(
	_In_ Spinlock_t* Lock));

/* SpinlockRelease
 * Releases the spinlock, and lets other threads access the lock */
CRTDECL(void,
SpinlockRelease(
	_In_ Spinlock_t* Lock));
_CODE_END

#endif //!__SPINLOCK_H__
