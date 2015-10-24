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
#include <List.h>
#include <Heap.h>
#include <DeviceManager.h>
#include <Vfs\Vfs.h>
#include <string.h>

/* Globals */
uint32_t GlbDmInitialized = 0;
DevId_t GlbDmIdentfier = 0;
list_t *GlbDmDeviceList = NULL;
Spinlock_t GlbDmLock;

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
		/* Register with Vfs */
		case DeviceStorage:
		{
			VfsRegisterDisk((MCoreStorageDevice_t*)Data);
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
			VfsUnregisterDisk((MCoreStorageDevice_t*)mDev->Data);
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