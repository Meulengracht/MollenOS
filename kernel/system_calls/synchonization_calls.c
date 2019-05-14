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
 * System call implementations (Synchronization)
 *
 */
#define __MODULE "SCIF"
//#define __TRACE

#include <os/osdefs.h>
#include <scheduler.h>
#include <handle.h>

/* ScWaitQueueCreate
 * Creates a new wait queue that can be used for synchronization purposes. */
OsStatus_t
ScWaitQueueCreate(
    _Out_ UUId_t* HandleOut)
{
    Collection_t* WaitQueue = CollectionCreate(KeyId);
    if (!WaitQueue) {
        return OsOutOfMemory;
    }
    *HandleOut = CreateHandle(HandleTypeWaitQueue, WaitQueue);
    return (*HandleOut != UUID_INVALID) ? OsSuccess : OsOutOfMemory;
}

/* ScWaitQueueBlock
 * Blocks a calling thread in a wait queue untill unblocked. */
OsStatus_t
ScWaitQueueBlock(
    _In_ UUId_t      WaitQueueHandle,
    _In_ spinlock_t* SyncObject,
    _In_ size_t      Timeout)
{
    Collection_t* WaitQueue = LookupHandleOfType(WaitQueueHandle, HandleTypeWaitQueue);
    if (!WaitQueue) {
        return OsDoesNotExist;
    }
    return SchedulerBlock(WaitQueue, SyncObject, Timeout);
}

/* ScWaitQueueUnblock
 * Unblocks a single thread in the wait queue. */
OsStatus_t
ScWaitQueueUnblock(
    _In_ UUId_t WaitQueueHandle)
{
    Collection_t* WaitQueue = LookupHandleOfType(WaitQueueHandle, HandleTypeWaitQueue);
    if (!WaitQueue) {
        return OsDoesNotExist;
    }
    return SchedulerUnblock(WaitQueue);
}
