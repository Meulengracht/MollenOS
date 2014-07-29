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
#include <arch.h>

/* Externs */
extern int _spinlock_acquire(spinlock_t *spinlock);
extern void _spinlock_release(spinlock_t *spinlock);

/* Acquire Spinlock */
int spinlock_acquire(spinlock_t *spinlock)
{
	int result = 0;

	/* Step 1. Acquire */
	result = _spinlock_acquire(spinlock);

	/* Step 2. Disable interrupts */
	spinlock->intr_state = interrupt_disable();

	return result;
}

/* Release Spinlock */
void spinlock_release(spinlock_t *spinlock)
{
	/* Step 1. Release spinlock */
	_spinlock_release(spinlock);

	/* Step 2. Enable interrupts */
	interrupt_set_state(spinlock->intr_state);
}

/* Acquire Spinlock, no interrupts */
int spinlock_acquire_nint(spinlock_t *spinlock)
{
	return _spinlock_acquire(spinlock);
}

/* Release spinlock, no interrupts */
void spinlock_release_nint(spinlock_t *spinlock)
{
	_spinlock_release(spinlock);
}