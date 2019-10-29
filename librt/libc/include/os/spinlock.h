/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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

enum {
    spinlock_plain     = 0,
    spinlock_recursive = 1
};

enum {
    spinlock_acquired = 0,  // means the lock has been acquired | still acquired
    spinlock_busy     = 1,  // lock is already taken
    spinlock_released = 2   // lock has been released
};

typedef struct spinlock {
    int          value;
    int          type;
    UUId_t       owner;
    _Atomic(int) references;
} spinlock_t;

#define _SPN_INITIALIZER_NP(Flags) { 0, Flags, UUID_INVALID, 0 }

_CODE_BEGIN

/**
 * * spinlock_init
 * This initializes a spinlock handle and sets it to default value (unlocked)
 */
CRTDECL(void,
spinlock_init(
	_In_ spinlock_t* lock,
    _In_ int         type));

/**
 * * spinlock_acquire
 * Acquires the spinlock while busy-waiting for it to be ready if neccessary
 */
CRTDECL(void,
spinlock_acquire(
	_In_ spinlock_t* lock));

/**
 * spinlock_try_acquire
 * Makes an attempt to acquire the spinlock without blocking 
 */
CRTDECL(int,
spinlock_try_acquire(
	_In_ spinlock_t* lock));

/**
 * * spinlock_release
 * Either releases the lock, or releases a reference to the lock. If the lock is still
 * hold due to nesting, the returned value is spinlock_acquired
 */
CRTDECL(int,
spinlock_release(
	_In_ spinlock_t* lock));

_CODE_END

#endif //!__SPINLOCK_H__
