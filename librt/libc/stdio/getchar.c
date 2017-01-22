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
#include <os/driver/input.h>
#include <os/ipc/ipc.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

/* The getchar */
int getchar(void)
{
	/* Message */
	MEventMessage_t Event;
	MEventInput_t Input;
	int Run = 1;

	/* Wait for input message, we need to discard 
	 * everything else as this is a polling op */
	while (Run) 
	{
		/* Listen for events */
		if (PipeRead(PIPE_EVENT, &Event, sizeof(MEventMessage_t))) {
			return -1;
		}

		/* The message must be of type input, otherwise
		 * we should trash the message :( */
		if (Event.Type == EVENT_INPUT) {
			PipeRead(PIPE_EVENT, &Input, sizeof(MEventInput_t));
			if (Input.Type == InputKeyboard
				&& (Input.Flags & INPUT_BUTTON_CLICKED)) {
				return (int)Input.Key;
			}
		}
		else {
			PipeRead(PIPE_EVENT, NULL, Event.Length);
		}
	}

	/* Done! */
	return 0;
}