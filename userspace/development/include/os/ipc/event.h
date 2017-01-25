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
 * MollenOS Inter-Process Communication Interface
 * - Event Procedure Call routines
 */

#ifndef _MOLLENOS_EVENT_H_
#define _MOLLENOS_EVENT_H_

/* Guard against inclusion */
#ifndef _MOLLENOS_IPC_H_
#error "You must include ipc.h and not this directly"
#endif

/* Includes 
 * - System */
#include <os/osdefs.h>

/* Includes
 * - C-Library */
#include <string.h>

/* The argument package that can be passed
 * to an IPC function request, we support up
 * to 5 arguments */
typedef struct _MEventArgument {
	int					Type;
	union {
		const void		*Buffer;
		size_t			Value;
	} Data;
	size_t				Length;
} EventArgument_t;

/* The base event message structure, any IPC
 * action going through pipes in MollenOS must
 * inherit from this structure for security */
typedef struct _MEventMessage {
	int					Version;
	int					Type;
	int					Port;
	size_t				Length;		/* Excluding this header */
	UUId_t				Sender;		/* Automatically set by OS */
	EventArgument_t		Arguments[IPC_MAX_ARGUMENTS];
} MEventMessage_t;

/* Cpp Guard */
#ifdef __cplusplus
extern "C" {
#endif

/* EVTInitialize 
 * Initializes a new EVT message of the 
 * given type and length */
static __CRT_INLINE void EVTInitialize(MEventMessage_t *Event, 
	int Version, int Port, int EventType)
{
	/* Zero out structure */
	memset((void*)Event, 0, sizeof(MEventMessage_t));

	/* Initialize some of the args */
	Event->Version = Version;
	Event->Type = EventType;
	Event->Port = Port;
}

/* EVTSetArgument
 * Adds a new argument for the EVT request at
 * the given argument index. It's not possible to override 
 * a current argument */
static __CRT_INLINE void EVTSetArgument(MEventMessage_t *Event,
	int Index, const void *Data, size_t Length)
{
	/* Sanitize the index and the
	 * current argument */
	if (Index >= IPC_MAX_ARGUMENTS
		|| Index < 0 || Event->Arguments[Index].Type != ARGUMENT_NOTUSED) {
		return;
	}

	/* Kind of argument? */
	if (Length <= sizeof(size_t)) {
		Event->Arguments[Index].Type = ARGUMENT_REGISTER;

		if (Length == 1) {
			Event->Arguments[Index].Data.Value = *((uint8_t*)Data);
		}
		else if (Length == 2) {
			Event->Arguments[Index].Data.Value = *((uint16_t*)Data);
		}
		else if (Length == 4) {
			Event->Arguments[Index].Data.Value = *((uint32_t*)Data);
		}
		else if (Length == 8) {
			Event->Arguments[Index].Data.Value = *((size_t*)Data);
		}
	}
	else {
		Event->Arguments[Index].Type = ARGUMENT_BUFFER;
		Event->Arguments[Index].Data.Buffer = Data;
		Event->Arguments[Index].Length = Length;
	}

	/* Increase total length of message */
	Event->Length += Length;
}

/* EVTExecute
 * Executes a new event to the desired target process
 * the process must be listening on PIPE_EVENT to be able
 * to recieve it. Events do not have replies */
_MOS_API OsStatus_t EVTExecute(MEventMessage_t *Event, UUId_t Target);

#ifdef __cplusplus
}
#endif

#endif //!_MOLLENOS_EVENT_H_