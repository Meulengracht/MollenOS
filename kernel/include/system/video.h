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
 * - Contains the shared kernel video interface
 *   that all sub-layers / architectures must conform to
 */

#ifndef _MCORE_VIDEO_H_
#define _MCORE_VIDEO_H_

/* Includes 
 * - Library */
#include <os/osdefs.h>
#include <os/spinlock.h>

/* Includes
 * - System */
#include <os/driver/contracts/video.h>
#include <arch.h>



// VideoType          (Text or Graphics)
// VideoDrawPixel     (At Position)
// VideoDrawCharacter (At Position)
// VideoPutCharacter  (Terminal)
// VideoQuery         (BootVideo Descriptor)



#endif //!_MCORE_VIDEO_H_
