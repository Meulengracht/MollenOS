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
* MollenOS Event Manager
*/

#ifndef _MCORE_EVENT_MANAGER_H_
#define _MCORE_EVENT_MANAGER_H_

/* Includes 
 * - System */
#include <os/osdefs.h>
#include <os/ipc/ipc.h>

/* Prototypes */
__CRT_EXTERN void EmRegisterSystemTarget(UUId_t ProcessId);

/* Write data to pointer pipe */
_CRT_EXPORT void EmCreateEvent(MEventMessage_t *Event);

#endif // !_MCORE_INPUT_MANAGER_H_
