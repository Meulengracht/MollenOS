/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
#include <Modules\ModuleManager.h>
#include <Semaphore.h>
#include <Scheduler.h>
#include <Timers.h>
#include <Heap.h>
#include <Log.h>
#include <DeviceManager.h>
#include <Vfs\Vfs.h>

/* C-Library */
#include <stddef.h>
#include <ds/list.h>
#include <string.h>

/* Devices Capable of requests */
#include <Devices\Disk.h>
#include <Devices\Timer.h>
#include <Devices\Video.h>
#include <Devices\Clock.h>

/* Globals */
MCoreEventHandler_t *GlbDeviceEventHandler = NULL;
MCoreDevice_t *GlbDmBootVideo = NULL;
int GlbDmInitialized = 0;
DevId_t GlbDmIdentfier = 0;
List_t *GlbDmDeviceList = NULL;
Spinlock_t GlbDmLock;

/* Request Thread - Prototype */
int DmRequestHandler(void *UserData, MCoreEvent_t *Event);

/* Setup */
void DmInit(void)
{
	/* Setup list */
	GlbDmDeviceList = ListCreate(KeyInteger, LIST_SAFE);

	/* Setup lock */
	SpinlockReset(&GlbDmLock);

	/* Done */
	GlbDmIdentfier = 0;
	GlbDmInitialized = 1;
}

/* Starts the DeviceManager request thread */
void DmStart(void)
{
	/* Debug */
	LogInformation("DRVM", "Starting Request Handler");

	/* Create event handler */
	GlbDeviceEventHandler = EventInit("Device Manager", DmRequestHandler, NULL);

	/* Is there a boot video? */
	if (GlbDmBootVideo != NULL)
		DmCreateDevice("Boot-Video", GlbDmBootVideo);
}

/* Create a request */
void DmCreateRequest(MCoreDeviceRequest_t *Request)
{
	/* Sanity */
	if (Request->Length > DEVICEMANAGER_MAX_IO_SIZE) {
		Request->ErrType = RequestInvalidParameters;
		return;
	}
	else {
		Request->ErrType = RequestNoError;
	}

	/* Deep call */
	EventCreate(GlbDeviceEventHandler, &Request->Base);
}

/* Wait for a request to complete */
void DmWaitRequest(MCoreDeviceRequest_t *Request, size_t Timeout)
{
	/* Deep Call */
	EventWait(&Request->Base, Timeout);
}

/* Request Thread */
int DmRequestHandler(void *UserData, MCoreEvent_t *Event)
{
	/* Vars */
	MCoreDeviceRequest_t *Request;
	DataKey_t Key;

	/* Unused */
	_CRT_UNUSED(UserData);

	/* Cast */
	Request = (MCoreDeviceRequest_t*)Event;
	Key.Value = (int)Request->DeviceId;

	/* Lookup Device */
	MCoreDevice_t *Dev =
		(MCoreDevice_t*)ListGetDataByKey(GlbDmDeviceList, Key, 0);

	/* Sanity */
	if (Dev == NULL)
	{
		/* Set status */
		Request->Base.State = EventFailed;
		Request->ErrType = RequestDeviceIsRemoved;

		/* Next! */
		return -1;
	}

	/* Set initial status */
	Request->Base.State = EventInProgress;

	/* Handle Event */
	switch (Request->Base.Type)
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
					|| Request->Length < 20) {
					Request->Base.State = EventFailed;
					Request->ErrType = RequestInvalidParameters;
				}
				else
				{
					/* Copy the first 20 bytes that contains stats */
					memcpy(Request->Buffer, Disk, 20);

					/* Done */
					Request->Base.State = EventOk;
					Request->ErrType = RequestNoError;
				}
			}
			else if (Dev->Type == DeviceVideo)
			{
				/* Cast again */
				MCoreVideoDevice_t *Video = (MCoreVideoDevice_t*)Dev->Data;

				/* Validate buffer */
				if (Request->Buffer == NULL
					|| Request->Length < sizeof(MCoreVideoDescriptor_t)) {
					Request->Base.State = EventFailed;
					Request->ErrType = RequestInvalidParameters;
				}
				else
				{
					/* Copy the descriptor */
					memcpy(Request->Buffer, &Video->Info, sizeof(MCoreVideoDescriptor_t));

					/* Done */
					Request->Base.State = EventOk;
					Request->ErrType = RequestNoError;
				}
			}
			else if (Dev->Type == DeviceClock)
			{
				/* Cast again */
				MCoreClockDevice_t *Clock = (MCoreClockDevice_t*)Dev->Data;

				/* Validate buffer */
				if (Request->Buffer == NULL
					|| Request->Length < sizeof(tm)) {
					Request->Base.State = EventFailed;
					Request->ErrType = RequestInvalidParameters;
				}
				else
				{
					/* Call */
					Clock->GetTime(Dev, (tm*)Request->Buffer);

					/* Done */
					Request->Base.State = EventOk;
					Request->ErrType = RequestNoError;
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
				if (Disk->Read(Dev, Request->SectorLBA, Request->Buffer, Request->Length) != 0) {
					Request->Base.State = EventFailed;
					Request->ErrType = RequestDeviceError;
				}
				else {
					Request->Base.State = EventOk;
					Request->ErrType = RequestNoError;
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
				if (Disk->Write(Dev, Request->SectorLBA, Request->Buffer, Request->Length) != 0) {
					Request->Base.State = EventFailed;
					Request->ErrType = RequestDeviceError;
				}
				else {
					Request->Base.State = EventOk;
					Request->ErrType = RequestNoError;
				}
			}

		} break;

		/* Install Driver */
		case RequestInstall:
		{
			/* Try to locate a vendor/device specific driver */
			MCoreModule_t *Driver = ModuleFindSpecific(Dev->VendorId, Dev->DeviceId);

			/* Did one exist? */
			if (Driver != NULL) {
				ModuleLoad(Driver, Dev);
			}
			else
			{
				/* Try to locate a generic driver
				* which is always better than nothing */
				Driver = ModuleFindGeneric(Dev->Class, Dev->Subclass);

				/* Find one? */
				if (Driver != NULL) {
					ModuleLoad(Driver, Dev);
				}
			}

		} break;

		default:
			break;
	}

	/* Clean up request if it was a install */
	if (Request->Base.Type == RequestInstall)
		Request->Base.Cleanup = 1;

	/* Done! */
	return 0;
}

DevId_t DmCreateDevice(char *Name, MCoreDevice_t *Device)
{
	/* DataKey for list */
	DataKey_t Key;

	/* Grap lock */
	SpinlockAcquire(&GlbDmLock);
	
	/* Set name and data */
	Device->Name = strdup(Name);
	Device->Id = GlbDmIdentfier;

	/* Increase */
	GlbDmIdentfier++;

	/* Release */
	SpinlockRelease(&GlbDmLock);

	/* Add to list */
	Key.Value = (int)Device->Id;
	ListAppend(GlbDmDeviceList, ListCreateNode(Key, Key, (void*)Device));

	/* Call some broadcast function so systems know a new device is avaiable
	 * depending on the device type */
	switch (Device->Type)
	{
		/* Give access to timer */
		case DeviceTimer:
		{
			/* Cast */
			MCoreTimerDevice_t *Timer = (MCoreTimerDevice_t*)Device->Data;
			Timer->ReportMs = TimersApplyMs;

		} break;

		/* Register with Vfs */
		case DeviceStorage:
		{
			/* Call */
			VfsRegisterDisk(Device->Id);

		} break;

		/* Unknown? Find a driver */
		case DeviceUnknown:
		{
			/* Create a driver request */
			MCoreDeviceRequest_t *Request = 
				(MCoreDeviceRequest_t*)kmalloc(sizeof(MCoreDeviceRequest_t));

			/* Reset request */
			memset(Request, 0, sizeof(MCoreDeviceRequest_t));

			/* Setup */
			Request->Base.Type = RequestInstall;
			Request->DeviceId = Device->Id;

			/* Send request */
			DmCreateRequest(Request);

		} break;

		/* No special actions */
		default:
			break;
	}

	/* Info Log */
	LogInformation("DRVM", "New Device: %s [at %u:%u:%u]", 
		Name, Device->Bus, Device->Device, Device->Function);

	/* Done */
	return Device->Id;
}

/* Request resource for device */
int DmRequestResource(MCoreDevice_t *Device, DeviceResourceType_t ResourceType)
{
	/* Sanity */
	if (Device == NULL)
		return -1;

	/* What kind of resource is requested? */
	if (ResourceType == ResourceIrq)
	{
		/* Request underlying Arch to get us an interrupt */
		return DeviceAllocateInterrupt(Device);
	}

	/* Eh, not supported */
	return -2;
}

/* Destroy device & cleanup resources */
void DmDestroyDevice(DevId_t DeviceId)
{
	/* Variables */
	MCoreDevice_t *mDev = NULL;
	DataKey_t Key;

	/* Lookup */
	Key.Value = (int)DeviceId;
	mDev = (MCoreDevice_t*)ListGetDataByKey(GlbDmDeviceList, Key, 0);

	/* Sanity */
	if (mDev == NULL)
		return;

	/* Remove device from list */
	ListRemoveByKey(GlbDmDeviceList, Key);

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

/* Find device data by id */
MCoreDevice_t *DmGetDevice(DeviceType_t Type)
{
	/* Find! */
	foreach(dNode, GlbDmDeviceList)
	{
		/* Cast */
		MCoreDevice_t *mDev = (MCoreDevice_t*)dNode->Data;

		/* Sanity */
		if (mDev->Type == Type)
			return mDev;
	}

	/* Dammn */
	return NULL;
}

/* Boot Video */
void DmRegisterBootVideo(MCoreDevice_t *Video)
{
	/* Set it */
	GlbDmBootVideo = Video;

	/* Now set it up */
	VideoBootInit((MCoreVideoDevice_t*)Video->Data);
}