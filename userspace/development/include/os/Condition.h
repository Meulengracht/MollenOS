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
* MollenOS C Library - Standard Mutex
* Contains Mutex Synchronization Methods
*/

#ifndef __CONDITIONS_CLIB_H__
#define __CONDITIONS_CLIB_H__

/* C-Library - Includes */
#include <crtdefs.h>
#include <stdint.h>

/* Synchronizations */
#include <os/Mutex.h>

/* CPP-Guard */
#ifdef __cplusplus
extern "C" {
#endif


/* The definition of a condition handle
 * used for primitive lock signaling */
#ifndef MTHREADCOND_DEFINED
#define MTHREADCOND_DEFINED
typedef unsigned int Condition_t;
#endif

/***********************
 * Condition Prototypes
 ***********************/

/* Instantiates a new condition and allocates
 * all required resources for the condition */
_MOS_API Condition_t *ConditionCreate(void);

/* Constructs an already allocated condition
 * handle and initializes it */
_MOS_API int ConditionConstruct(Condition_t *Cond);

/* Destroys a conditional variable and 
 * wakes up all remaining sleepers */
_MOS_API void ConditionDestroy(Condition_t *Cond);

/* Signal the condition and wakes up a thread
 * in the queue for the condition */
_MOS_API int ConditionSignal(Condition_t *Cond);

/* Broadcast a signal to all condition waiters
 * and wakes threads up waiting for the cond */
_MOS_API int ConditionBroadcast(Condition_t *Cond);

/* Waits for condition to be signaled, and 
 * acquires the given mutex, using multiple 
 * mutexes for same condition is undefined behaviour */
_MOS_API int ConditionWait(Condition_t *Cond, Mutex_t *Mutex);

/* This functions as the ConditionWait, 
 * but also has a timeout specified, so that 
 * we get waken up if the timeout expires (in seconds) */
_MOS_API int ConditionWaitTimed(Condition_t *Cond, Mutex_t *Mutex, time_t Expiration);

/* CPP Guard */
#ifdef __cplusplus
}
#endif

#endif //!__CONDITIONS_CLIB_H__