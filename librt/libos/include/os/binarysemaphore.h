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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS MCore - Semaphore (Binary) Support Definitions & Structures
 * - This header describes the binary semaphore-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _BINARYSEMAPHORE_INTERFACE_H_
#define _BINARYSEMAPHORE_INTERFACE_H_

#include <os/osdefs.h>
#include <os/mutex.h>
#include <os/condition.h>

typedef struct BinarySemaphore {
    Mutex_t     Mutex;
    Condition_t Condition;
    int         Value;
} BinarySemaphore_t;

/**
 * @brief Initializes the semaphore value to either 0 or 1
 * @param binarySemaphore
 * @param value
 * @return
 */
CRTDECL(oserr_t,
BinarySemaphoreInitialize(
    _In_ BinarySemaphore_t* binarySemaphore,
    _In_ int                value));

/**
 * @brief Reinitializes the semaphore with a value of 0.
 * @param binarySemaphore
 */
CRTDECL(void,
BinarySemaphoreReset(
        _In_ BinarySemaphore_t* binarySemaphore));

/**
 * @brief Post event to a single thread waiting for an event.
 * @param binarySemaphore
 */
CRTDECL(void,
BinarySemaphorePost(
        _In_ BinarySemaphore_t* binarySemaphore));

/**
 * @brief Post event to all threads waiting for an event
 * @param binarySemaphore
 */
CRTDECL(void,
BinarySemaphorePostAll(
        _In_ BinarySemaphore_t* binarySemaphore));

/**
 * @brief Wait on semaphore until semaphore has value 0.
 * @param binarySemaphore
 */
CRTDECL(void,
BinarySemaphoreWait(
        _In_ BinarySemaphore_t* binarySemaphore));

#endif //!_BINARYSEMAPHORE_INTERFACE_H_
