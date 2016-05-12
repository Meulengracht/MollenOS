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
* MollenOS Input Manager
*/

/* Includes */
#include <Arch.h>
#include <InputManager.h>
#include <RingBuffer.h>
#include <stddef.h>

/* Globals */
int GlbEmInitialized = 0;
PId_t GlbEmWindowManager = 0;

/* Initialise Event Manager */
void EmInit(void)
{
	/* Set initialized */
	GlbEmInitialized = 1;
	GlbEmWindowManager = 0xFFFFFFFF;
}

/* Register */
void EmRegisterSystemTarget(PId_t ProcessId)
{
	/* Sanity, NO OVERRIDES */
	if (ProcessId != 0xFFFFFFFF
		&& GlbEmWindowManager != 0xFFFFFFFF)
		return;

	/* Set */
	GlbEmWindowManager = ProcessId;
}

/* Write data to pointer pipe */
void EmCreateEvent(MEventMessageBase_t *Event)
{
	/* Temp Buffer */
	uint8_t NotRecycleBin[64];

	/* Sanity */
	if (GlbEmInitialized != 1)
		EmInit();

	/* Sanity - More ! */
	if (GlbEmWindowManager != 0xFFFFFFFF) 
	{
		/* Get process */
		MCoreProcess_t *Process = PmGetProcess(GlbEmWindowManager);
		
		/* Force space in buffer */
		while (PipeBytesLeft(Process->Pipe) < (int)(Event->Length))
			PipeRead(Process->Pipe, Event->Length, (uint8_t*)&NotRecycleBin, 0);

		/* Set sender as system */
		Event->Sender = 0;
		
		/* Write data to pipe */
		PipeWrite(Process->Pipe, Event->Length, (uint8_t*)Event);
	}
}