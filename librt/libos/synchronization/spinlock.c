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

/* Includes 
 * - System */
#include <os/spinlock.h>
#include <os/syscall.h>

/* Includes 
 * - Library */
#include <stddef.h>

/* Externs
 * Access to platform specifics */
__EXTERN int _spinlock_acquire(Spinlock_t *Spinlock);
__EXTERN int _spinlock_test(Spinlock_t *Spinlock);
__EXTERN void _spinlock_release(Spinlock_t *Spinlock);

/* SpinlockReset
 * This initializes a spinlock
 * handle and sets it to default
 * value (unlocked) */
OsStatus_t 
SpinlockReset(
	_In_ Spinlock_t *Lock)
{
	/* Sanity */
	if (Lock == NULL) {
		return OsError;
	}

	/* Reset it to 0 */
	*Lock = SPINLOCK_INIT;
	return OsSuccess;
}

/* SpinlockAcquire
 * Acquires the spinlock while busy-waiting
 * for it to be ready if neccessary */
OsStatus_t
SpinlockAcquire(
	_In_ Spinlock_t *Lock)
{
	/* Sanitize params */
	if (Lock == NULL) {
		return OsError;
	}

	/* Redirect to platform specific implementation */
	return _spinlock_acquire(Lock) == 1 ? OsSuccess : OsError;
}

/* SpinlockTryAcquire
 * Makes an attempt to acquire the
 * spinlock without blocking */
OsStatus_t
SpinlockTryAcquire(
	_In_ Spinlock_t *Lock)
{
	/* Sanity */
	if (Lock == NULL) {
		return OsError;
	}

	/* Redirect to platform specific implementation */
	return _spinlock_test(Lock) == 1 ? OsSuccess : OsError;
}

/* SpinlockRelease
 * Releases the spinlock, and lets
 * other threads access the lock */
OsStatus_t 
SpinlockRelease(
	_In_ Spinlock_t *Lock)
{
	/* Sanity */
	if (Lock == NULL) {
		return OsError;
	}

	/* Redirect to platform specific implementation */
	_spinlock_release(Lock);
	return OsSuccess;
}
