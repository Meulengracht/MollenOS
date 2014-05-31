/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS x86-32 Architecture Header
*/

#ifndef _MCORE_X86_ARCH_
#define _MCORE_X86_ARCH_

/* Architecture Includes */
#include <crtdefs.h>
#include <multiboot.h>

/* Architecture Definitions */
#define ARCHITECTURE_VERSION	"0.0.1a"
#define ARCHITECTURE_NAME		"x86-32"
#define STD_VIDEO_MEMORY		0xB8000

/* Architecture Prototypes, you should define 
 * as many as these as possible */

/* Components */
_CRT_EXTERN void video_init(multiboot_info_t *bootinfo);
_CRT_EXTERN int video_putchar(int character);

/* Driver Interface */

#endif // !_MCORE_X86_ARCH_
