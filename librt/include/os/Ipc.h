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
 * MollenOS InterProcess Comm Interface
 */

#ifndef _MOLLENOS_IPC_H_
#define _MOLLENOS_IPC_H_

/* Includes 
 * - System */
#include <os/osdefs.h>
#include <os/virtualkeycodes.h>

/* Predefined system pipe-ports that should not
 * be used by user pipes. Trying to open new pipes
 * on these ports will result in error */
#define PIPE_SERVER				0x8000
#define PIPE_WINDOWMANAGER		0x8001

/* The different types of base-events 
 * that can occur in IPC messages. All messages
 * sent through pipes should inherit the base */
typedef enum _MEventType
{
	/* System Events 
	 * - Server Types */
	EventServerControl,
	EventServerCommand,

	/* System Events
	 * -Window Types */
	EventWindowControl,
	EventWindowInput,

	/* Users can derive their own
	 * base-event types from this one
	 * by making their base enum inherit 
	 * this definition */
	 EventUser

} MEventType_t;

/* The base event message structure, any IPC
 * action going through pipes in MollenOS must
 * inherit from this structure for security */
typedef struct _MEventMessage
{
	/* Type */
	MEventType_t Type;

	/* This is the length of the
	 * entire message, including this header */
	size_t Length;

	/* Message Source
	 * The sender of this message, this is automatically
	 * set by the operating system */
	IpcComm_t Sender;

} MEventMessage_t;

/* Cpp Guard */
#ifdef __cplusplus
extern "C" {
#endif

#ifndef _KERNEL_API

/***********************
* IPC Prototypes
***********************/

/* IPC - Open - Pipe
 * Opens a new communication pipe on the given
 * port for this process, if one already exists
 * SIGPIPE is signaled */
_MOS_API int PipeOpen(int Port);

/* IPC - Close - Pipe
* Closes an existing communication pipe on the given
* port for this process, if one doesn't exists
* SIGPIPE is signaled */
_MOS_API int PipeClose(int Port);

/* IPC - Read
 * This returns -1 if something went wrong reading
 * a message from the message queue, otherwise it returns 0
 * and fills the structures with information about the message */
_MOS_API int PipeRead(int Pipe, void *Buffer, size_t Length);

/* IPC - Send
 * Returns -1 if message failed to send
 * Returns -2 if message-target didn't exist
 * Returns 0 if message was sent correctly to target */
_MOS_API int PipeSend(IpcComm_t Target, int Port, void *Message, size_t Length);

#endif //!_KERNEL_API

#ifdef __cplusplus
}
#endif

#endif //!_MOLLENOS_IPC_H_
