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
* MollenOS C Library - Get Character
*/

/* Includes */
#include <os/ipc/window.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

/* The getchar */
int getchar(void)
{
	/* Message */
	union {
		MEventMessage_t Base;
		MWindowInput_t Input;
	} Message;
	uint8_t *MessagePointer = (uint8_t*)&Message;
	int Run = 1;

	/* Wait for input message, we need to discard 
	 * everything else as this is a polling op */
	while (Run) 
	{
		/* Get message */
		if (PipeRead(PIPE_WINDOWMANAGER, MessagePointer, sizeof(MEventMessage_t)))
			return -1;

		/* Increase message pointer by base
		 * bytes read */
		MessagePointer += sizeof(MEventMessage_t);

		/* Handle Message */
		if (Message.Base.Type == EventWindowInput) {
			if (!PipeRead(PIPE_WINDOWMANAGER, MessagePointer,
				sizeof(MWindowInput_t) - sizeof(MEventMessage_t))) {
				if (Message.Input.Type == WindowInputKeyboard) {
					if (Message.Input.Flags & MCORE_INPUT_BUTTON_CLICKED) {
						return (int)Message.Input.Key;
					}
				}
			}
		}
		else {
			/* Trash Message */
			PipeRead(PIPE_WINDOWMANAGER, NULL, 
				Message.Base.Length - sizeof(MEventMessage_t));
		}
	}

	/* Done! */
	return 0;
}