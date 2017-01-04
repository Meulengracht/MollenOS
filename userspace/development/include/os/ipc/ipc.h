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
 * - Shared definitions
 * - Remote Procedure Call routines
 * - Event Procedure Call routines
 * - Pipe routines
 */

#ifndef _MOLLENOS_IPC_H_
#define _MOLLENOS_IPC_H_

/* Includes
 * - C-Library */
#include <os/osdefs.h>

/* IPC Declaration definitions that can
 * be used by the different IPC systems */
#define IPC_DECL_FUNCTION(FunctionNo)			(int)FunctionNo
#define IPC_DECL_EVENT(EventNo)					(int)EventNo
#define IPC_MAX_ARGUMENTS						5

/* Predefined system pipe-ports that should not
 * be used by user pipes. Trying to open new pipes
 * on these ports will result in error */
#define PIPE_DEFAULT					0
#define PIPE_EVENT						1

/* Predefined system events that is common for
 * all userspace applications, these primarily consists
 * of input and/or window events */
#define EVENT_INPUT						IPC_DECL_EVENT(0)
#define EVENT_WINDOW_REPAINT			IPC_DECL_EVENT(1)

/* Include Systems */
#include <os/ipc/event.h>
#include <os/ipc/rpc.h>
#include <os/ipc/pipe.h>

#endif //!_MOLLENOS_IPC_H_
