/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS X86-32 Spinlock Code
*/

/* Includes */
#include <Arch.h>

/* Externs to assembly */
extern int _spinlock_acquire(Spinlock_t *Spinlock);
extern void _spinlock_release(Spinlock_t *Spinlock);

/* Acquire Spinlock */
OsStatus_t SpinlockAcquire(Spinlock_t *Spinlock)
{
	int result = 0;

	/* Step 1. Acquire */
	result = _spinlock_acquire(Spinlock);

	/* Step 2. Disable interrupts */
	Spinlock->IntrState = InterruptDisable();

	return (result == 1) ? OS_STATUS_OK : OS_STATUS_FAIL;
}

/* Release Spinlock */
void SpinlockRelease(Spinlock_t *Spinlock)
{
	/* Step 1. Release spinlock */
	_spinlock_release(Spinlock);

	/* Step 2. Enable interrupts */
	InterruptRestoreState(Spinlock->IntrState);
}

/* Acquire Spinlock, no interrupts */
OsStatus_t SpinlockAcquireNoInt(Spinlock_t *Spinlock)
{
	return _spinlock_acquire(Spinlock);
}

/* Release spinlock, no interrupts */
void SpinlockReleaseNoInt(Spinlock_t *Spinlock)
{
	_spinlock_release(Spinlock);
}