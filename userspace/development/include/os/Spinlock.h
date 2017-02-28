/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS MCore - Spinlock Support Definitions & Structures
 * - This header describes the base spinlock-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _SPINLOCK_INTERFACE_H_
#define _SPINLOCK_INTERFACE_H_

/* Includes
 * - System */
#include <os/osdefs.h>

/* The definition of a spinlock handle
 * used for primitive lock access */
typedef int Spinlock_t;
#define SPINLOCK_INIT			0

/* Start one of these before function prototypes */
_CODE_BEGIN

/* SpinlockReset
 * This initializes a spinlock
 * handle and sets it to default
 * value (unlocked) */
MOSAPI 
OsStatus_t 
SpinlockReset(
	_In_ Spinlock_t *Lock);

/* SpinlockAcquire
 * Acquires the spinlock while busy-waiting
 * for it to be ready if neccessary */
MOSAPI 
OsStatus_t
SpinlockAcquire(
	_In_ Spinlock_t *Lock);

/* SpinlockTryAcquire
 * Makes an attempt to acquire the
 * spinlock without blocking */
MOSAPI 
OsStatus_t
SpinlockTryAcquire(
	_In_ Spinlock_t *Lock);

/* SpinlockRelease
 * Releases the spinlock, and lets
 * other threads access the lock */
MOSAPI 
OsStatus_t 
SpinlockRelease(
	_In_ Spinlock_t *Lock);

_CODE_END

#endif //!_SPINLOCK_INTERFACE_H_
