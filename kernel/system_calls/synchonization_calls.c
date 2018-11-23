/* MollenOS
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
 * MollenOS MCore - System Calls
 */
#define __MODULE "SCIF"
//#define __TRACE

#include <os/osdefs.h>
#include <scheduler.h>
#include <heap.h>

/* ScConditionCreate
 * Create a new shared handle 
 * that is unique for a condition variable */
OsStatus_t
ScConditionCreate(
    _Out_ Handle_t *Handle)
{
    // Sanitize input
    if (Handle == NULL) {
        return OsError;
    }
 
    *Handle = (Handle_t)kmalloc(sizeof(Handle_t));
    return OsSuccess;
}

/* ScConditionDestroy
 * Destroys a shared handle
 * for a condition variable */
OsStatus_t
ScConditionDestroy(
    _In_ Handle_t Handle)
{
    kfree(Handle);
    return OsSuccess;
}

/* ScSignalHandle
 * Signals a handle for wakeup 
 * This is primarily used for condition
 * variables and semaphores */
OsStatus_t
ScSignalHandle(
    _In_ uintptr_t *Handle)
{
    return SchedulerHandleSignal(Handle);
}

/* Signals a handle for wakeup all
 * This is primarily used for condition
 * variables and semaphores */
OsStatus_t
ScSignalHandleAll(
    _In_ uintptr_t *Handle)
{
    SchedulerHandleSignalAll(Handle);
    return OsSuccess;
}

/* ScWaitForObject
 * Waits for a signal relating to the above function, this
 * function uses a timeout. Returns OsError on timed-out */
OsStatus_t
ScWaitForObject(
    _In_ uintptr_t *Handle,
    _In_ size_t Timeout)
{
    // Store reason for waking up
    int WakeReason = SchedulerThreadSleep(Handle, Timeout);
    if (WakeReason == SCHEDULER_SLEEP_OK) {
        return OsSuccess;
    }
    else {
        return OsError;
    }
}
