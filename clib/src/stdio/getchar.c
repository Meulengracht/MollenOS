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
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <os/MollenOS.h>

/* The getchar */
int getchar(void)
{
	/* Message */
	MEventMessage_t Message;

	/* Wait for input message, we need to discard 
	 * everything else as this is a polling op */
	while (1) 
	{
		/* Get message */
		if (MollenOSMessageWait(&Message))
			return -1;

		/* Handle Message */
		if (Message.Base.Type == EventInput
			&& Message.EventButton.Type == InputKeyboard) {
			if (Message.EventButton.State == MCORE_INPUT_BUTTON_CLICKED) {
				return (int)Message.EventButton.Data;
			}
		}
	}

	return 0;
}