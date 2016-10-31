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
* MollenOS Window Ipc Structures - Sapphire
*/

#ifndef _SAPPHIRE_WINDOWIPC_H_
#define _SAPPHIRE_WINDOWIPC_H_

/* Includes */
#include <crtdefs.h>
#include <stdint.h>

#include <os/MollenOS.h>

/* IPC Structures */

/***********************
 * Window Creation IPC Message
 ***********************/
typedef struct _MIPCWindowCreate
{
	/* Request Information */
	Rect_t Dimensions;
	int Flags;

	/* Response Information 
	 * - Window Id */
	int WindowId;

	/* The dimensions returned: 
	 * - Window Dimensions 
	 * - Backbuffer Dimensions */
	Rect_t WndDimensions;
	Rect_t BbDimensions;

	/* The backbuffer information
	 * - Size in bytes
	 * - Pointer */
	void *Backbuffer;
	size_t BackbufferSize;

} IPCWindowCreate_t;

/***********************
 * Window Query IPC Message
 ***********************/
typedef struct _MIPCWindowQuery
{
	/* Request Information */
	IpcComm_t Target;

	/* Response Information */
	char Name[64];
	Rect_t Dimensions;

} IPCWindowQuery_t;

#endif //!_SAPPHIRE_WINDOWIPC_H_