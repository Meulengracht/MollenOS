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
 * MollenOS Video Interface (Boot)
 * - Contains the shared kernel video functionality
 *   and structures that is primarily used by MCore
 */

#ifndef _MCORE_VIDEO_H_
#define _MCORE_VIDEO_H_

/* Includes 
 * - Library */
#include <os/osdefs.h>
#include <os/spinlock.h>
#include <os/contracts/video.h>

/* VideoInitialize
 * Initializes boot-video environment untill a more
 * complete driver can take-over the screen */
KERNELAPI
OsStatus_t
KERNELABI
VideoInitialize(void);

/* VideoQuery
 * Renders a character with default colors
 * at the current terminal position */
KERNELAPI
OsStatus_t
KERNELABI
VideoQuery(
	_Out_ VideoDescriptor_t *Descriptor);

#endif //!_MCORE_VIDEO_H_
