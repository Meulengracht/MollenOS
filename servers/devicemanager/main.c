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
 * MollenOS MCore - Device Manager
 * - Initialization + Event Mechanism
 */

/* Includes
 * - System */
#include <os/driver/device.h>
#include <os/mollenos.h>
#include <ds/list.h>
#include <bus.h>

/* Includes
 * - C-Library */
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>

/* Globals */
List_t *GlbDeviceList = NULL;
DevId_t GlbDeviceIdGen = 0;
int GlbInitialized = 0;
int GlbRun = 0;

/* Entry point of a server
 * this handles setup and enters the event-queue
 * the data passed is a system informations structure
 * that contains information about the system */
int ServerMain(void *Data)
{
	/* Storage for message */
	MRemoteCall_t Message;

	/* Save */
	_CRT_UNUSED(Data);

	/* Setup list */
	GlbDeviceList = ListCreate(KeyInteger, LIST_SAFE);

	/* Init variables */
	GlbDeviceIdGen = 0;
	GlbInitialized = 1;
	GlbRun = 1;

	/* Register us with server manager */
	RegisterServer(__DEVICEMANAGER_TARGET);

	/* Enumerate bus controllers/devices */
	BusEnumerate();

	/* Enter event queue */
	while (GlbRun) 
	{
		if (RPCListen(&Message) == OsNoError) {
			/* Which function is called? */
			switch (Message.Function) {

				/* Handles registration of a new device */
				case __DEVICEMANAGER_REGISTERDEVICE: {
					/* Variables for result */
					DevId_t Result; 

					/* Evaluate request, but don't free
					 * the allocated device storage, we need it */
					Message.Arguments[0].InUse = 0;
					Result = RegisterDevice((MCoreDevice_t*)Message.Arguments[0].Buffer, NULL);

					/* Write the result back to the caller */
					PipeSend(Message.Sender, Message.Port, 
						&Result, sizeof(DevId_t));
				} break;
				case __DEVICEMANAGER_UNREGISTERDEVICE: {

				} break;
				case __DEVICEMANAGER_QUERYDEVICE: {

				} break;
				case __DEVICEMANAGER_IOCTLDEVICE: {

				} break;

				default: {
					if (Message.Length != 0) {
						PipeRead(PIPE_DEFAULT, NULL, Message.Length);
					}
				}
			}

			/* Cleanup message */
			RPCCleanup(&Message);
		}
		else {
			/* Wtf? */
		}
	}

	/* Done, no error, return 0 */
	return 0;
}

/* Device Registering
 * Allows registering of a new device in the
 * device-manager, and automatically queries
 * for a driver for the new device */
DevId_t RegisterDevice(MCoreDevice_t *Device, const char *Name)
{
	/* Variables */
	DevId_t DeviceId = GlbDeviceIdGen++;
	DataKey_t Key;

	/* Update name and print debug information */
	if (Name != NULL) {
		memcpy(&Device->Name[0], Name, strlen(Name));
		MollenOSSystemLog("Registered device %s", Name);
	}

	/* Update id, add to list */
	Key.Value = DeviceId;
	Device->Id = DeviceId;
	ListAppend(GlbDeviceList, ListCreateNode(Key, Key, Device));

	/* Now, we want to try to find a driver
	 * for the new device */
	if (InstallDriver(Device) == OsError) {
	}

	/* Done with processing of the new device */
	return DeviceId;
}
