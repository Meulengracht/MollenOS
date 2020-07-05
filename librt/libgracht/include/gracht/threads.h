/* MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * Gracht Threads Type Definitions & Structures
 * - This header describes the base threads-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __GRACHT_THREADS_H__
#define __GRACHT_THREADS_H__

#if defined(MOLLENOS)
#include <threads.h>
#elif defined(__linux__)
#include <pthread.h>

typedef pthread_mutex_t mtx_t;

#define thrd_success 0

#define mtx_plain NULL

#define mtx_init    pthread_mutex_init
#define mtx_destroy pthread_mutex_destroy
#define mtx_trylock pthread_mutex_trylock
#define mtx_lock    pthread_mutex_lock
#define mtx_unlock  pthread_mutex_unlock

typedef pthread_cond_t cnd_t;

#define cnd_init(cnd) pthread_cond_init(cnd, NULL)
#define cnd_destroy   pthread_cond_destroy
#define cnd_wait      pthread_cond_wait
#define cnd_signal    pthread_cond_signal

#else
#error "Undefined platform for threads"
#endif

#endif // !__GRACHT_THREADS_H__
