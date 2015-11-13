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
* MollenOS - Device Manager
*/

/* Includes */
#include <Semaphore.h>
#include <Scheduler.h>
#include <List.h>
#include <Timers.h>
#include <Heap.h>
#include <DeviceManager.h>
#include <Vfs\Vfs.h>
#include <string.h>

/* Devices Capable of requests */
#include <Devices\Disk.h>
#include <Devices\Timer.h>

/* Globals */
uint32_t GlbDmInitialized = 0;
DevId_t GlbDmIdentfier = 0;
list_t *GlbDmDeviceList = NULL;
Spinlock_t GlbDmLock; 

/* Request Handler Vars */
Semaphore_t *GlbDmEventLock = NULL;
list_t *GlbDmEventQueue = NULL;

/* Request Thread - Prototype */
void DmRequestHandler(void *Args);

/* Setup */
void DmInit(void)
{
	/* Setup list */
	GlbDmDeviceList = list_create(LIST_SAFE);

	/* Setup lock */
	SpinlockReset(&GlbDmLock);

	/* Done */
	GlbDmIdentfier = 0;
	GlbDmInitialized = 1;
}

/* Starts the DeviceManager request thread */
void DmStart(void)
{
	/* Create the signal & Request queue */
	GlbDmEventLock = SemaphoreCreate(0);
	GlbDmEventQueue = list_create(LIST_SAFE);

	/* Spawn the thread */
	ThreadingCreateThread("DeviceRequestHandler", DmRequestHandler, NULL, 0);
}

/* Create a request */
void DmCreateRequest(MCoreDeviceRequest_t *Request)
{
	/* Append it to our request list */
	list_append(GlbDmEventQueue, list_create_node(0, Request));

	/* Notify request thread */
	SemaphoreV(GlbDmEventLock);

	/* We shall enter sleep meanwhile */
	if (!Request->IsAsync)
	{
		SchedulerSleepThread((Addr_t*)Request);
		_ThreadYield();
	}
}

/* Request Thread */
void DmRequestHandler(void *Args)
{
	/* Vars */
	list_node_t *lNode;
	MCoreDeviceRequest_t *Request;

	/* Unused */
	_CRT_UNUSED(Args);

	while (1)
	{
		/* Acquire Semaphore */
		SemaphoreP(GlbDmEventLock);

		/* Pop Request */
		lNode = list_pop_front(GlbDmEventQueue);

		/* Sanity */
		if (lNode == NULL)
			continue;

		/* Cast */
		Request = (MCoreDeviceRequest_t*)lNode->data;

		/* Free the node */
		kfree(lNode);

		/* Again, sanity */
		if (Request == NULL)
			continue;

		/* Lookup Device */
		MCoreDevice_t *Dev =
			(MCoreDevice_t*)list_get_data_by_id(GlbDmDeviceList, Request->DeviceId, 0);

		/* Sanity */
		if (Dev == NULL)
		{
			/* Set status */
			Request->Status = RequestDeviceIsRemoved;

			/* We are done, wakeup */
			if (!Request->IsAsync)
				SchedulerWakeupOneThread((Addr_t*)Request);

			/* Next! */
			continue;
		}

		/* Set initial status */
		Request->Status = RequestOk;

		/* Handle Event */
		switch (Request->Type)
		{
			/* Read from Device */
			case RequestQuery:
			{
				/* Sanity type */
				if (Dev->Type == DeviceStorage)
				{
					/* Cast again */
					MCoreStorageDevice_t *Disk = (MCoreStorageDevice_t*)Dev->Data;

					/* Validate buffer */
					if (Request->Buffer == NULL
						|| Request->Length < 20)
						Request->Status = RequestInvalidParameters;
					else
					{
						/* Copy the first 20 bytes that contains stats */
						memcpy(Request->Buffer, Disk, 20);
					}
				}

			} break;

			/* Read from Device */
			case RequestRead:
			{
				/* Sanity type */
				if (Dev->Type == DeviceStorage)
				{
					/* Cast again */
					MCoreStorageDevice_t *Disk = (MCoreStorageDevice_t*)Dev->Data;

					/* Validate parameters */

					/* Perform */
					if (Disk->Read(Disk->DiskData, Request->SectorLBA, Request->Buffer, Request->Length) < 0)
					{
						/* Error */
						Request->Status = RequestDeviceError;
					}
				}

			} break;

			/* Write to Device */
			case RequestWrite:
			{
				/* Sanity type */
				if (Dev->Type == DeviceStorage)
				{
					/* Cast again */
					MCoreStorageDevice_t *Disk = (MCoreStorageDevice_t*)Dev->Data;

					/* Validate parameters */

					/* Perform */
					if (Disk->Write(Disk->DiskData, Request->SectorLBA, Request->Buffer, Request->Length) < 0)
					{
						/* Error */
						Request->Status = RequestDeviceError;
					}
				}

			} break;

			default:
				break;
		}

		/* We are done, wakeup */
		if (!Request->IsAsync)
			SchedulerWakeupOneThread((Addr_t*)Request);
	}
}

DevId_t DmCreateDevice(char *Name, uint32_t Type, void *Data)
{
	/* Allocate a new structure */
	MCoreDevice_t *mDev = (MCoreDevice_t*)kmalloc(sizeof(MCoreDevice_t));

	/* Grap lock */
	SpinlockAcquire(&GlbDmLock);
	
	/* Set name and data */
	mDev->Name = strdup(Name);
	mDev->Id = GlbDmIdentfier;
	mDev->Type = Type;
	mDev->Data = Data;

	/* Increase */
	GlbDmIdentfier++;

	/* Release */
	SpinlockRelease(&GlbDmLock);

	/* Add to list */
	list_append(GlbDmDeviceList, list_create_node(mDev->Id, (void*)mDev));

	/* Call some broadcast function so systems know a new device is avaiable
	 * depending on the device type */
	switch (Type)
	{
		/* Give access to timer */
		case DeviceTimer:
		{
			/* Cast */
			MCoreTimerDevice_t *Timer = (MCoreTimerDevice_t*)Data;
			Timer->ReportMs = TimersApplyMs;

		} break;

		/* Register with Vfs */
		case DeviceStorage:
		{
			/* Call */
			VfsRegisterDisk(mDev->Id);

		} break;

		/* No special actions */
		default:
			break;
	}

	/* Done */
	return mDev->Id;
}

void DmDestroyDevice(DevId_t DeviceId)
{
	/* Lookup */
	MCoreDevice_t *mDev = (MCoreDevice_t*)list_get_data_by_id(GlbDmDeviceList, DeviceId, 0);

	/* Sanity */
	if (mDev == NULL)
		return;

	/* Remove device from list */
	list_remove_by_id(GlbDmDeviceList, DeviceId);

	/* Call some broadcast function so systems know a device is being removed
	* depending on the device type */
	switch (mDev->Type)
	{
		/* Register with Vfs */
		case DeviceStorage:
		{
			VfsUnregisterDisk(mDev->Id, 1);
		} break;

		/* No special actions */
		default:
			break;
	}

	/* Destroy lock & free name */
	kfree(mDev->Name);

	/* Cleanup structure */
	kfree(mDev);
}