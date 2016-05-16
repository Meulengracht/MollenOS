/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS - Graphical UI Functions
*/

/* Includes */
#include <os/MollenOS.h>
#include <os/Syscall.h>
#include <os/Thread.h>

/* C Library */
#include <stddef.h>

/* Kernel Guard */
#ifdef LIBC_KERNEL
void __SpinlockLibCEmpty(void)
{
}
#else

/* Externs to assembly */
extern int _spinlock_acquire(Spinlock_t *Spinlock);
extern void _spinlock_release(Spinlock_t *Spinlock);

/* Spinlock Reset
 * This initializes a spinlock
 * handle and sets it to default
 * value (unlocked) */
void SpinlockReset(Spinlock_t *Lock)
{
	/* Sanity */
	if (Lock == NULL)
		return;

	/* Reset it to 0 */
	*Lock = 0;
}

/* Spinlock Acquire
 * Acquires the spinlock, this
 * is a blocking operation */
int SpinlockAcquire(Spinlock_t *Lock)
{
	/* Sanity */
	if (Lock == NULL)
		return 0;

	/* Deep call */
	return _spinlock_acquire(Lock);
}

/* Spinlock Release
 * Releases the spinlock, and lets
 * other threads access the lock */
void SpinlockRelease(Spinlock_t *Lock)
{
	/* Sanity */
	if (Lock == NULL)
		return;

	/* Deep call */
	_spinlock_release(Lock);
}

#endif