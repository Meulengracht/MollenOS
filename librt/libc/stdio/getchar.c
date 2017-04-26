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
	// Variables
	MRemoteCall_t Event;
	MInput_t *Input = NULL;
	int Character = 0;
	int Run = 1;

	// Wait for input message, we need to discard 
	// everything else as this is a polling op
	while (Run) {
		if (RPCListen(&Event) == OsSuccess) {
			Input = (MInput_t*)Event.Arguments[0].Data.Buffer;
			if (Event.Function == EVENT_INPUT) {
				if (Input->Type == InputKeyboard
					&& (Input->Flags & INPUT_BUTTON_CLICKED)) {
					Character = (int)Input->Key;
					Run = 0;
				}
			}
		}
		RPCCleanup(&Event);
	}

	// Return the resulting read character
	return Character;
}
