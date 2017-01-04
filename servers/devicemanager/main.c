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

/* Includes
 * - C-Library */
#include <stddef.h>
#include <ds/list.h>
#include <os/mollenos.h>
#include <string.h>
#include <ctype.h>

/* Globals */
List_t *GlbDeviceList = NULL;
DevId_t GlbDeviceIdGen = 0;
int GlbInitialized = 0;
int GlbRun = 0;

/* Prototypes
 * - Drvm Prototypes */


/* Entry point of a server
 * this handles setup and enters the event-queue
 * the data passed is a system informations structure
 * that contains information about the system */
int ServerMain(void *Data)
{
	/* Storage for message */
	MEventMessage_t Message;
	uint8_t *MessagePointer = (uint8_t*)&Message;

	/* Save */
	_CRT_UNUSED(Data);

	/* Setup list */
	GlbDeviceList = ListCreate(KeyInteger, LIST_SAFE);

	/* Init variables */
	GlbDeviceIdGen = 0;
	GlbInitialized = 1;
	GlbRun = 1;

	/* Register us with server manager */


	/* Enter event queue */
	while (GlbRun) {
		if (!PipeRead(PIPE_DEFAULT, MessagePointer, sizeof(MEventMessage_t)))
		{
			/* Increase message pointer by base
			 * bytes read */
			MessagePointer += sizeof(MEventMessage_t);

			/* Control message or response? */
			if (Message.Base.Type == EventServerControl)
			{
				/* Read the rest of the message */
				if (!PipeRead(PIPE_SERVER, MessagePointer, 
					sizeof(MServerControl_t) - sizeof(MEventMessage_t))) {
					switch (Message.Control.Type)
					{
						case IpcRegisterDevice: {
							
						} break;

						case IpcUnregisterDevice: {

						} break;

						case IpcQueryDevice: {

						} break;

						case IpcControlDevice: {

						} break;

						/* Invalid is not for us */
						default:
							break;
					}
				}
			}
			else if (Message.Base.Type == EventServerCommand)
			{
				/* Read the rest of the message */
				if (!PipeRead(PIPE_SERVER, MessagePointer,
					sizeof(MCoreDeviceRequest_t) - sizeof(MEventMessage_t))) {
					switch (Message.Command.Type)
					{
						default:
							break;
					}
				}
			}
		}
		else {
			/* Wtf? */
		}
	}

	/* Done, no error, return 0 */
	return 0;
}
