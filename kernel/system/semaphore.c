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

/* Includes */
#include <assert.h>
#include <scheduler.h>
#include <semaphore.h>
#include <heap.h>

/* Externs */
extern void _yield(void);

/* Creates an semaphore */
semaphore_t *semaphore_create(int value)
{
	semaphore_t *semaphore;

	/* Sanity */
	assert(value >= 0);

	/* Allocate */
	semaphore = (semaphore_t*)kmalloc(sizeof(semaphore_t));
	semaphore->value = value;
	semaphore->creator = threading_get_thread_id();
	spinlock_reset(&semaphore->lock);

	return semaphore;
}

void semaphore_destroy(semaphore_t *sem)
{
	/* Wake up all */
	scheduler_wakeup_all((addr_t*)sem);

	/* Free it */
	kfree(sem);
}

/* Acquire Lock */
void semaphore_P(semaphore_t *sem)
{
	interrupt_status_t int_state;

	/* Lock */
	int_state = interrupt_disable();
	spinlock_acquire(&sem->lock);

	sem->value--;
	if (sem->value < 0) 
	{
		/* Important to release lock before we do this */
		spinlock_release(&sem->lock);
		scheduler_sleep_thread((addr_t*)sem);
		_yield();
	}
	else
		spinlock_release(&sem->lock);

	/* Release */
	interrupt_set_state(int_state);
}

/* Release Lock */
void semaphore_V(semaphore_t *sem)
{
	interrupt_status_t int_state;

	/* Lock */
	int_state = interrupt_disable();
	spinlock_acquire(&sem->lock);


	sem->value++;
	if (sem->value <= 0)
		scheduler_wakeup_one((addr_t*)sem);

	spinlock_release(&sem->lock);
	
	/* Release */
	interrupt_set_state(int_state);
}