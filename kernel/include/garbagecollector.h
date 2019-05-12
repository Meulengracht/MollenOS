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
 * Garbage Collector
 * - Makes it possible for regular cleanup in the kernel or regular maintiance.
 */

#ifndef _MCORE_GARBAGECOLLECTOR_H_
#define _MCORE_GARBAGECOLLECTOR_H_

#include <os/osdefs.h>

typedef OsStatus_t (*GcHandler_t)(void*);

/* GcInitialize
 * Initializes the garbage-collector system */
KERNELAPI void KERNELABI
GcInitialize(void);

/* GcRegister
 * Registers a new gc-handler that will be run
 * when new work is available, returns the unique id
 * for the new handler */
KERNELAPI UUId_t KERNELABI
GcRegister(
    _In_ GcHandler_t Handler);

/* GcUnregister
 * Removes a previously registed handler by its id */
KERNELAPI OsStatus_t KERNELABI
GcUnregister(
    _In_ UUId_t Handler);

/* GcSignal
 * Signals new garbage for the specified handler */
KERNELAPI OsStatus_t KERNELABI
GcSignal(
    _In_ UUId_t Handler,
    _In_ void *Data);

#endif //!_MCORE_GARBAGECOLLECTOR_H_