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
* MollenOS Module Shared Library
*/

/* Includes */
#include <stddef.h>
#include <Module.h>
#include <Semaphore.h>
#include <Mutex.h>

/* Typedefs */
typedef Semaphore_t *(*__semcreate)(int Value);
typedef void (*__semdestroy)(Semaphore_t *Semaphore);
typedef void(*__semp)(Semaphore_t *Semaphore);
typedef void(*__semv)(Semaphore_t *Semaphore);

typedef Mutex_t *(*__muxcreate)(void);
typedef void(*__muxconstruct)(Mutex_t *Mutex);
typedef void(*__muxdestruct)(Mutex_t *Mutex);
typedef void(*__muxlock)(Mutex_t *Mutex);
typedef void(*__muxunlock)(Mutex_t *Mutex);

/* Create Semaphore Wrapper */
Semaphore_t *SemaphoreCreate(int Value)
{
	return ((__semcreate)GlbFunctionTable[kFuncSemaphoreCreate])(Value);
}

/* Destroy Semaphore Wrapper */
void SemaphoreDestroy(Semaphore_t *Semaphore)
{
	((__semdestroy)GlbFunctionTable[kFuncSemaphoreDestroy])(Semaphore);
}

/* Acquire(?) Semaphore */
void SemaphoreP(Semaphore_t *Semaphore)
{
	((__semp)GlbFunctionTable[kFuncSemaphoreP])(Semaphore);
}

/* Release(?) Semaphore */
void SemaphoreV(Semaphore_t *Semaphore)
{
	((__semv)GlbFunctionTable[kFuncSemaphoreV])(Semaphore);
}

/* Create Mutex Wrapper */
Mutex_t *MutexCreate(void)
{
	return ((__muxcreate)GlbFunctionTable[kFuncMutexCreate])();
}

/* Construct Mutex Wrapper */
void MutexConstruct(Mutex_t *Mutex)
{
	((__muxconstruct)GlbFunctionTable[kFuncMutexConstruct])(Mutex);
}

/* Destruct Mutex Wrapper */
void MutexDestruct(Mutex_t *Mutex)
{
	((__muxdestruct)GlbFunctionTable[kFuncMutexDestruct])(Mutex);
}

/* Lock Mutex Wrapper */
void MutexLock(Mutex_t *Mutex)
{
	((__muxlock)GlbFunctionTable[kFuncMutexLock])(Mutex);
}

/* Unlock Mutex Wrapper */
void MutexUnlock(Mutex_t *Mutex)
{
	((__muxunlock)GlbFunctionTable[kFuncMutexUnlock])(Mutex);
}