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
 * MollenOS MCore - Threading Pool Support Definitions & Structures
 * - This header describes the base threadingpool-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _THREADINGPOOL_INTERFACE_H_
#define _THREADINGPOOL_INTERFACE_H_

/* Includes
 * - Library */
#include <os/osdefs.h>
#include <os/thread.h>

/* ThreadPool Definitions
 * These include structure prototypes and customization through
 * definitions */
typedef struct _ThreadPool ThreadPool_t;
#define THREADPOOL_DEFAULT_WORKERS			-1 // Call this to initialize with default number of workers

/* Cpp guard to avoid name-mangling */
_CODE_BEGIN

/* ThreadPoolInitialize 
 * Initializes a new thread-pool with the given number of threads */
MOSAPI
OsStatus_t
MOSABI
ThreadPoolInitialize(
	_In_ int NumThreads,
	_Out_ ThreadPool_t **ThreadPool);

/* ThreadPoolAddWork
 * Takes an action and its argument and adds it to the threadpool's job queue. 
 * If you want to add to work a function with more than one arguments then
 * a way to implement this is by passing a pointer to a structure. */
MOSAPI
OsStatus_t
MOSABI
ThreadPoolAddWork(
	_In_ ThreadPool_t *ThreadPool,
	_In_ ThreadFunc_t Function,
	_In_ void *Argument);

/* ThreadPoolWait
 * Will wait for all jobs - both queued and currently running to finish.
 * Once the queue is empty and all work has completed, the calling thread
 * (probably the main program) will continue. */
MOSAPI
OsStatus_t
MOSABI
ThreadPoolWait(
	_In_ ThreadPool_t *ThreadPool);

/* ThreadPoolPause
 * The threads will be paused no matter if they are idle or working.
 * The threads return to their previous states once thpool_resume
 * is called. */
MOSAPI
OsStatus_t
MOSABI
ThreadPoolPause(
	_In_ ThreadPool_t *ThreadPool);

/* ThreadPoolResume
 * Unpauses all threads if they are paused. */
MOSAPI
OsStatus_t
MOSABI
ThreadPoolResume(
	_In_ ThreadPool_t *ThreadPool);

/* ThreadPoolDestroy
 * This will wait for the currently active threads to finish and then 'kill'
 * the whole threadpool to free up memory. */
MOSAPI
OsStatus_t
MOSABI
ThreadPoolDestroy(
	_In_ ThreadPool_t *ThreadPool);

/* ThreadPoolGetWorkingCount
 * Returns the number of working threads are the threads that are performing work (not idle). */
MOSAPI
size_t
MOSABI
ThreadPoolGetWorkingCount(
	_In_ ThreadPool_t *ThreadPool);

_CODE_END

#endif //!_THREADINGPOOL_INTERFACE_H_
