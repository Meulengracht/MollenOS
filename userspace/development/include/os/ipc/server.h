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

#ifndef _MOLLENOS_IPC_SERVER_H_
#define _MOLLENOS_IPC_SERVER_H_

/* Includes
 * - System */
#include <os/ipc.h>
#include <os/osdefs.h>

/* The types of server messages 
 * These are the control messages that
 * all servers should support */
typedef enum _MServerControlType {
	ServerCtrlPing,
	ServerCtrlPong,
	ServerCtrlRestart,
	ServerCtrlQuit
} MServerControlType_t;

/* Base structure for window control 
 * messages, the types of control messages
 * are defined above by MWindowControlType_t */
typedef struct _MServerControl
{
	/* Base */
	MEventMessage_t Header;

	/* Message Type */
	MServerControlType_t Type;

} MServerControl_t;

#endif //!_MOLLENOS_IPC_SERVER_H_
