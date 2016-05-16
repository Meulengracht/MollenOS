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
* MollenOS C Library - Standard OS Threading Header
* Contains threading methods + synchronization
*/

#ifndef __THREADING_CLIB__
#define __THREADING_CLIB__

/* C-Library - Includes */
#include <crtdefs.h>
#include <stdint.h>

/* Definitons */

/* The definition of a spinlock handle
 * used for primitive lock access */
#ifndef MSPINLOCK_DEFINED
#define MSPINLOCK_DEFINED
typedef int Spinlock_t;
#endif

/* Structures */


/* Prototypes */

/***********************
* Spinlock Prototypes
***********************/

/* Spinlock Reset
 * This initializes a spinlock
 * handle and sets it to default
 * value (unlocked) */
_MOS_API void SpinlockReset(Spinlock_t *Lock);

/* Spinlock Acquire
 * Acquires the spinlock, this
 * is a blocking operation.
 * Returns 1 on lock success */
_MOS_API int SpinlockAcquire(Spinlock_t *Lock);

/* Spinlock Release
 * Releases the spinlock, and lets
 * other threads access the lock */
_MOS_API void SpinlockRelease(Spinlock_t *Lock);

#endif //!__THREADING_CLIB__