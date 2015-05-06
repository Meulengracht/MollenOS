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
* MollenOS Synchronization
* Semaphores
*/

#ifndef _MCORE_SEMAPHORE_H_
#define _MCORE_SEMAPHORE_H_

/* Includes */
#include <Arch.h>
#include <crtdefs.h>
#include <stdint.h>

/* Structures */
typedef struct _Semaphore 
{
	/* Spinlock */
	Spinlock_t Lock;

	/* Value */
	volatile int Value;

	/* Semaphore Creator */
	TId_t Creator;

} Semaphore_t;

/* Prototypes */
_CRT_EXTERN Semaphore_t *SemaphoreCreate(int Value);
_CRT_EXTERN void SemaphoreDestroy(Semaphore_t *Semaphore);
_CRT_EXTERN void SemaphoreP(Semaphore_t *Semaphore);
_CRT_EXTERN void SemaphoreV(Semaphore_t *Semaphore);

#endif // !_MCORE_SEMAPHORE_H_
