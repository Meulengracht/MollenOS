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
 * MollenOS MCore - Condition Support Definitions & Structures
 * - This header describes the base condition-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _CONDITIONS_INTERFACE_H_
#define _CONDITIONS_INTERFACE_H_

/* Includes
 * - System */
#include <os/osdefs.h>
#include <os/mutex.h>

/* The definition of a condition handle
 * used for primitive lock signaling */
typedef size_t Condition_t;

/* Start one of these before function prototypes */
_CODE_BEGIN

/* ConditionCreate
 * Instantiates a new condition and allocates
 * all required resources for the condition */
MOSAPI 
Condition_t *
ConditionCreate(void);

/* ConditionConstruct
 * Constructs an already allocated condition
 * handle and initializes it */
MOSAPI 
OsStatus_t 
ConditionConstruct(
	_In_ Condition_t *Cond);

/* ConditionDestroy
 * Destroys a conditional variable and 
 * wakes up all remaining sleepers */
MOSAPI 
OsStatus_t 
ConditionDestroy(
	_In_ Condition_t *Cond);

/* ConditionSignal
 * Signal the condition and wakes up a thread
 * in the queue for the condition */
MOSAPI 
OsStatus_t 
ConditionSignal(
	_In_ Condition_t *Cond);

/* ConditionBroadcast
 * Broadcast a signal to all condition waiters
 * and wakes threads up waiting for the cond */
MOSAPI 
OsStatus_t 
ConditionBroadcast(
	_In_ Condition_t *Cond);

/* ConditionWait
 * Waits for condition to be signaled, and 
 * acquires the given mutex, using multiple 
 * mutexes for same condition is undefined behaviour */
MOSAPI 
OsStatus_t 
ConditionWait(
	_In_ Condition_t *Cond,
	_In_ Mutex_t *Mutex);

/* ConditionWaitTimed
 * This functions as the ConditionWait, 
 * but also has a timeout specified, so that 
 * we get waken up if the timeout expires (in seconds) */
MOSAPI 
OsStatus_t 
ConditionWaitTimed(
	_In_ Condition_t *Cond, 
	_In_ Mutex_t *Mutex, 
	_In_ time_t Expiration);

_CODE_END

#endif //!_CONDITIONS_INTERFACE_H_
