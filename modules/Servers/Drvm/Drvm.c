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
* MollenOS MCore - Device Manager
* - Initialization + Event Mechanism
*/

/* Includes
 * - System */
#include <Servers/Devices.h>
#include <Module.h>

/* Includes
 * - C-Library */
#include <ds/list.h>
#include <os/mollenos.h>
#include <string.h>
#include <ctype.h>

/* Globals */
List_t *GlbDmDeviceList = NULL;
DevId_t GlbDmIdentfier = 0;
int GlbDmInitialized = 0;
int GlbDrvmRun = 0;

/* Prototypes
 * - Drvm Prototypes */


/* Entry point of a module or server
 * this handles setup and enters the event-queue
 * Initializes the virtual filesystem and
 * all resources related, and starts the DRVMEventLoop */
MODULES_API void ModuleInit(void *Data)
{
	/* Save */
	_CRT_UNUSED(Data);

	/* Setup list */
	GlbDmDeviceList = ListCreate(KeyInteger, LIST_SAFE);

	/* Init variables */
	GlbDmIdentfier = 0;
	GlbDmInitialized = 1;
	GlbDrvmRun = 1;

	/* Register us with server manager */

	/* Enter event queue */
	while (GlbDrvmRun)
	{
		/* Storage for message */
		MCoreDeviceRequest_t Request;

		/* Wait for event */
		if (!MollenOSMessageWait((MEventMessage_t*)&Request))
		{
			/* Control message or response? */
			if (Request.Base.Type == EventServerControl)
			{
				/* Control message
				* Cast the type to generic */
				MEventMessageGeneric_t *ControlMsg = (MEventMessageGeneric_t*)&Request;

				/* Switch command */
				switch (ControlMsg->Type)
				{



					/* Invalid is not for us */
					default:
						break;
				}
			}
			else if (Request.Base.Type == EventServerCommand)
			{
				/* Handle request */
				switch (Request.Type)
				{
					
					default:
						break;
				}

				/* Switch request to response */
				Request.Base.Type = EventServerResponse;

				/* Send structure return */
				MollenOSMessageSend(Request.Base.Sender, &Request, sizeof(MCoreDeviceRequest_t));
			}
		}
		else {
			/* Wtf? */
		}
	}
}
